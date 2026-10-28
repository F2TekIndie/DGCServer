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

// Test doubles for the network and cache boundaries. The cache implementation
// is currently a stub; this checks routing, not cache synchronization.
mqClient::mqClient( Configuration const& config ) :_config(config), _client(_ioc){}
mqClient::~mqClient(){}
void mqClient::publish( std::string const& topic, std::string const& payload ){
    ++publishCount;
    receiver->HandleMessage( topic, payload );
}
DroneCache::DroneCache( std::shared_ptr<database> db ) :_db(db){}
void DroneCache::StoreDevice( dronegroundcontrol::Device const& value ){ devices.push_back(value); }
void DroneCache::StoreAttribute( dronegroundcontrol::AttributeRequest const& value ){ attributes.push_back(value); }
void DroneCache::StoreTimeseries( dronegroundcontrol::TimeseriesRequest const& value ){ timeseries.push_back(value); }

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
        dgcSubscriptions handler( cache );
        receiver = &handler;

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
            handler.HandleMessage( *topic, std::string(1, '\xff') );
        }
        handler.HandleMessage( "/unknown", "ignored" );
        Require( devices.size() == 1 && attributes.size() == 1 && timeseries.size() == 1, "Invalid payloads must not reach cache" );

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
        Require( drone._position.empty(), "Consume history after commit" );
        db->StoreDrone(drone);
        Require( check.Scalar("SELECT count(*) FROM timeseries") == 4, "Empty history must not duplicate samples" );
        Require( check.Scalar(("SELECT count(*) FROM timeseries WHERE timestamp_ms=" + std::to_string(stamp.time_since_epoch().count())).c_str()) == 2, "Exact timestamp" );

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
        Require( drone._position.size() == 1, "Keep history after rollback" );
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
