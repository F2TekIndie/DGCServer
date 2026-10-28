#include "mqsubscriptions.h"
#include "dronecache.h"
#include "db.h"
#include "dronemanager.h"
#include "config.h"
#include "mqtt.h"
#include "devices.pb.h"
#include "attributes.pb.h"
#include "timeseries.pb.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace{
    dgcSubscriptions* receiver = nullptr;
    int publishCount = 0;
    std::vector<dronegroundcontrol::Device> devices;
    std::vector<dronegroundcontrol::AttributeRequest> attributes;
    std::vector<dronegroundcontrol::TimeseriesRequest> timeseries;

    void Require( bool condition, char const* message ){
        if ( !condition ){
            throw std::runtime_error( message );
        }
    }

    struct TestFile{
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("dgc-persistence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
        ~TestFile(){
            std::error_code error;
            std::filesystem::remove( path, error );
        }
    };

    struct Connection{
        sqlite3* db = nullptr;
        explicit Connection( std::filesystem::path const& path ){
            Require( sqlite3_open(path.string().c_str(), &db) == SQLITE_OK, "Open test database" );
        }
        ~Connection(){ sqlite3_close( db ); }
        void Execute( std::string const& sql ){
            if ( sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK ){
                throw std::runtime_error( sqlite3_errmsg(db) );
            }
        }
        sqlite3_int64 Scalar( char const* sql ){
            sqlite3_stmt* statement = nullptr;
            Require( sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK, "Prepare query" );
            const int result = sqlite3_step( statement );
            const auto value = sqlite3_column_int64( statement, 0 );
            sqlite3_finalize( statement );
            Require( result == SQLITE_ROW, "Read query result" );
            return value;
        }
    };
}

// Replace only the network boundary; exercise the real cache and its worker.
mqClient::mqClient( Configuration const& config ) :_config(config), _client(_ioc){}
mqClient::~mqClient(){}
void mqClient::publish( std::string const& topic, std::string const& payload ){
    ++publishCount;
    if ( topic == dronesdk::mqtt::EP_DEVICE ){
        dronegroundcontrol::Device value;
        Require( value.ParseFromString(payload), "Device payload" );
        devices.push_back(value);
    }
    else if ( topic == dronesdk::mqtt::EP_ATTRIBUTES ){
        dronegroundcontrol::AttributeRequest value;
        Require( value.ParseFromString(payload), "Attribute payload" );
        attributes.push_back(value);
    }
    else if ( topic == dronesdk::mqtt::EP_TIMESERIES ){
        dronegroundcontrol::TimeseriesRequest value;
        Require( value.ParseFromString(payload), "Timeseries payload" );
        timeseries.push_back(value);
    }
    receiver->HandleMessage( topic, payload );
}

int main( int argc, char** argv ){
    try{
        Require( argc == 2, "Schema argument required" );
        TestFile file;
        Connection check( file.path );
        std::ifstream schema( argv[1] );
        Require( schema.good(), "Read schema file" );
        check.Execute( std::string(std::istreambuf_iterator<char>(schema), {}) );
        Configuration config;
        config._dbName = file.path.string();
        auto db = std::make_shared<database>( config );
        auto cache = std::make_shared<DroneCache>( db );
        auto handler = std::make_unique<dgcSubscriptions>( cache );
        receiver = handler.get();

        mqClient mqtt( config );
        DroneManager simulator( mqtt );
        simulator.Init( 1 );
        Require( publishCount == 3, "One device, attribute batch and timeseries batch publish" );
        Require( devices.size() == 1 && attributes.size() == 1 && timeseries.size() == 1, "Topic routing" );
        Require( devices[0].id() == 1 && devices[0].client_id().id() == 99 && devices[0].name() == "Drone_1", "Device identity" );
        auto const& attributeBatch = attributes[0];
        Require( attributeBatch.device().id() == 1 && !attributeBatch.has_attrib(), "Attribute batch envelope" );
        Require( attributeBatch.attributes().timeseries_size() == 2, "Attribute batch size" );
        auto const& online = attributeBatch.attributes().timeseries(0);
        auto const& state = attributeBatch.attributes().timeseries(1);
        Require( online.name() == "online" && online.has_bvalue() && !online.bvalue(), "Online attribute" );
        Require( state.name() == "state" && state.has_ivalue() && state.ivalue() == 0, "State attribute" );
        auto const& history = timeseries[0];
        Require( history.device().id() == 1 && !history.has_data() && history.timeseries().timeseries_size() == 2, "Timeseries batch envelope" );
        auto const& latitude = history.timeseries().timeseries(0);
        auto const& longitude = history.timeseries().timeseries(1);
        Require( latitude.name() == "latitude" && latitude.has_svalue(), "Latitude sample" );
        Require( longitude.name() == "longitude" && longitude.has_svalue(), "Longitude sample" );
        Require( latitude.timestamp() == longitude.timestamp() && std::stoll(latitude.timestamp()) > 0, "Shared timestamp" );
        for (auto const* topic : {&dronesdk::mqtt::EP_DEVICE, &dronesdk::mqtt::EP_ATTRIBUTES, &dronesdk::mqtt::EP_TIMESERIES}){
            handler->HandleMessage( *topic, std::string(1, '\xff') );
        }
        handler->HandleMessage( "/unknown", "ignored" );
        Require( devices.size() == 1 && attributes.size() == 1 && timeseries.size() == 1, "Invalid payloads must not reach cache" );

        // Incomplete coordinate messages are rejected, not stored as pending.
        auto pair = timeseries[0];
        pair.mutable_device()->set_id(2);
        auto incomplete = pair;
        incomplete.mutable_timeseries()->mutable_timeseries()->RemoveLast();
        bool incompleteRejected = false;
        try{ cache->StoreTimeseries(incomplete); }catch (std::invalid_argument const&){ incompleteRejected = true; }
        Require(incompleteRejected, "Reject incomplete coordinate pair");
        for (auto& sample : *pair.mutable_timeseries()->mutable_timeseries()){
            sample.set_timestamp("1700000000123");
            sample.set_svalue("0");
        }
        cache->StoreTimeseries(pair);
        cache->StoreTimeseries(pair); // Replay must not duplicate the pair.
        pair.mutable_timeseries()->mutable_timeseries(0)->set_svalue("1");
        cache->StoreTimeseries(pair); // Same key updates coordinates.
        auto invalid = attributes[0];
        invalid.mutable_device()->set_id(3);
        invalid.mutable_attributes()->mutable_timeseries(1)->set_svalue("wrong type");
        bool invalidRejected = false;
        try{ cache->StoreAttribute(invalid); }catch (std::invalid_argument const&){ invalidRejected = true; }
        Require(invalidRejected, "Reject invalid attribute batch");
        // Destruction joins the worker and completes the final flush.
        receiver = nullptr;
        handler.reset();
        cache.reset();
        Require(check.Scalar("SELECT count(*) FROM device") == 2, "Cache accepts unknown drones keyed by ID");
        Require(check.Scalar("SELECT count(*) FROM attributes WHERE device_id=1") == 3, "Persist cached current state");
        Require(check.Scalar("SELECT count(*) FROM timeseries WHERE device_id=1") == 2, "Persist simulator position pair once");
        Require(check.Scalar("SELECT count(*) FROM timeseries WHERE device_id=2 AND timestamp_ms=1700000000123") == 2, "Store coordinate batch at same timestamp");
        Require(check.Scalar("SELECT count(*) FROM timeseries WHERE device_id=2 AND name='latitude' AND string_value='1'") == 1, "Correct existing coordinate pair");
        Require(check.Scalar("SELECT count(*) FROM device WHERE id=3") == 0, "Bad batch cannot create partial drone");
        check.Execute("DELETE FROM timeseries; DELETE FROM attributes; DELETE FROM device; DELETE FROM client;");

        // Verify the new snapshot persistence API independently of the cache.
        Drone drone{};
        drone._droneId = devices[0].id();
        drone._clientId = devices[0].client_id().id();
        drone._name = devices[0].name();
        drone._online = online.bvalue();
        drone._state = state.ivalue();
        const auto stamp = std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{std::stoll(latitude.timestamp())}};
        TimeseriesPos position{stamp, std::stod(longitude.svalue()), std::stod(latitude.svalue())};
        drone._position.push_back(position);
        db->StoreDrone( drone );
        Require( check.Scalar("SELECT count(*) FROM device WHERE id=1 AND client_id=99") == 1, "Automatic identity creation" );
        Require( check.Scalar("SELECT count(*) FROM attributes WHERE device_id=1") == 3, "Snapshot attribute count" );
        drone._online = true;
        drone._state = 5;
        position._posLat = 59;
        position._timestamp += std::chrono::milliseconds(1);
        drone._position.push_back(position);
        db->StoreDrone( drone );
        Require( check.Scalar("SELECT bool_value FROM attributes WHERE name='online'") == 1, "Update online" );
        Require( check.Scalar("SELECT uint32_value FROM attributes WHERE name='state'") == 5, "Update state" );
        Require( check.Scalar("SELECT latitude FROM attributes WHERE name='position'") == 59, "Update position" );
        Require( check.Scalar("SELECT count(*) FROM attributes") == 3, "Upsert must not duplicate attributes" );
        Require( check.Scalar("SELECT count(*) FROM timeseries") == 4, "Persist both coordinate pairs" );
        Require( drone._position.size() == 2 && drone._persistedPositions == 2, "Retain committed history" );
        db->StoreDrone(drone);
        Require( check.Scalar("SELECT count(*) FROM timeseries") == 4, "Repeated snapshot must not duplicate history" );
        Require( check.Scalar(("SELECT count(*) FROM timeseries WHERE timestamp_ms=" + std::to_string(stamp.time_since_epoch().count())).c_str()) == 2, "Exact timestamp" );

        auto replay = drone;
        replay._persistedPositions = 0;
        replay._position[0]._posLat = 57;
        db->StoreDrone(replay);
        Require(check.Scalar("SELECT count(*) FROM timeseries") == 4, "Database key deduplicates replay");

        position._posLat = std::numeric_limits<double>::quiet_NaN();
        drone._position.push_back(position);
        bool rejected = false;
        try{ db->StoreDrone(drone); }catch (std::invalid_argument const&){ rejected = true; }
        Require( rejected, "Reject invalid coordinates" );
        drone._position.back()._posLat = 58;
        drone._name = "Changed";
        check.Execute("CREATE TRIGGER reject_state BEFORE UPDATE ON attributes WHEN NEW.name='state' BEGIN SELECT RAISE(ABORT,'test'); END");
        rejected = false;
        try{ db->StoreDrone(drone); }catch (std::runtime_error const&){ rejected = true; }
        Require( rejected, "Propagate database failure" );
        Require( drone._position.size() == 3 && drone._persistedPositions == 2, "Keep history and progress after rollback" );
        Require( check.Scalar("SELECT count(*) FROM timeseries") == 4, "Rollback history" );
        Require( check.Scalar("SELECT count(*) FROM device WHERE name='Drone_1'") == 1, "Rollback identity changes" );
        Require( check.Scalar("SELECT latitude FROM attributes WHERE name='position'") == 59, "Rollback snapshot changes" );
        Require( check.Scalar("SELECT count(*) FROM pragma_foreign_key_check") == 0, "Foreign key integrity" );
        std::cout << "MQTT routing and StoreDrone persistence checks passed\n";
    }
    catch ( std::exception const& error ){
        std::cerr << error.what() << '\n';
        return 1;
    }
}
