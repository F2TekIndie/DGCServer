#include "mqsubscriptions.h"
#include "dronecache.h"
#include "droneidentity.h"
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
        sqlite3_int64 Scalar( std::string const& sql ){
            sqlite3_stmt* statement = nullptr;
            Require( sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK, "Prepare query" );
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
    if ( !topic.ends_with( "/attributes" ) && !topic.ends_with( "/timeseries" ) ){
        dronegroundcontrol::Device value;
        Require( value.ParseFromString(payload), "Device payload" );
        Require( topic == "/device/" + value.id(), "Per-drone metadata topic" );
        devices.push_back(value);
    }
    else if ( topic.ends_with( "/attributes" ) ){
        dronegroundcontrol::AttributeRequest value;
        Require( value.ParseFromString(payload), "Attribute payload" );
        Require( topic == "/device/" + value.device().id() + "/attributes", "Per-drone attributes topic" );
        attributes.push_back(value);
    }
    else if ( topic.ends_with( "/timeseries" ) ){
        dronegroundcontrol::TimeseriesRequest value;
        Require( value.ParseFromString(payload), "Timeseries payload" );
        Require( topic == "/device/" + value.device().id() + "/timeseries", "Per-drone timeseries topic" );
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

        const auto subscriptions = handler->Subscriptions();
        Require( subscriptions.size() == 4, "Four subscription filters" );
        Require( subscriptions[0].topic_filter == "/device/+", "Metadata filter" );
        Require( subscriptions[1].topic_filter == "/status", "Status unchanged" );
        Require( subscriptions[2].topic_filter == "/device/+/attributes", "Attributes filter" );
        Require( subscriptions[3].topic_filter == "/device/+/timeseries", "Timeseries filter" );

        mqClient mqtt( config );
        DroneManager simulator( mqtt );
        const auto firstId = cache->CreateDevice(99)._droneId;
        const auto externalId = NewDroneId();
        const auto secondId = cache->CreateDevice(99)._droneId;
        simulator.Init( {firstId, secondId} );
        Require( publishCount == 6, "Two drones each publish metadata, attributes and timeseries" );
        Require( devices.size() == 2 && attributes.size() == 2 && timeseries.size() == 2, "Topic routing" );
        Require( devices[0].id() == firstId && devices[0].client_id().id() == 99 && devices[0].name() == "Drone_1", "Device identity" );
        auto const& attributeBatch = attributes[0];
        Require( attributeBatch.device().id() == firstId && !attributeBatch.has_attrib(), "Attribute batch envelope" );
        Require( attributeBatch.attributes().timeseries_size() == 2, "Attribute batch size" );
        auto const& online = attributeBatch.attributes().timeseries(0);
        auto const& state = attributeBatch.attributes().timeseries(1);
        Require( online.name() == "online" && online.has_bvalue() && !online.bvalue(), "Online attribute" );
        Require( state.name() == "state" && state.has_ivalue() && state.ivalue() == 0, "State attribute" );
        auto const& history = timeseries[0];
        Require( history.device().id() == firstId && !history.has_data() && history.timeseries().timeseries_size() == 2, "Timeseries batch envelope" );
        auto const& latitude = history.timeseries().timeseries(0);
        auto const& longitude = history.timeseries().timeseries(1);
        Require( latitude.name() == "latitude" && latitude.has_svalue(), "Latitude sample" );
        Require( longitude.name() == "longitude" && longitude.has_svalue(), "Longitude sample" );
        Require( latitude.timestamp() == longitude.timestamp() && std::stoll(latitude.timestamp()) > 0, "Shared timestamp" );
        for (auto const& topic : {dronesdk::mqtt::DeviceTopic(firstId), dronesdk::mqtt::AttributesTopic(firstId), dronesdk::mqtt::TimeseriesTopic(firstId)}){
            handler->HandleMessage( topic, std::string(1, '\xff') );
        }
        handler->HandleMessage( "/unknown", "ignored" );
        Require( devices.size() == 2 && attributes.size() == 2 && timeseries.size() == 2, "Invalid payloads must not reach cache" );

        // Routing uses the topic shape, while the payload remains authoritative for identity.
        auto renamed = devices[0];
        renamed.set_name("Payload identity");
        handler->HandleMessage("/device/" + externalId, renamed.SerializeAsString());
        Require(cache->GetDrone(devices[0])->_Drone._name == "Payload identity", "Metadata uses payload identity");
        auto topicDevice = devices[0];
        topicDevice.set_id(externalId);
        Require(!cache->GetDrone(topicDevice), "Topic identity does not create or select a drone");
        auto changed = attributes[0];
        changed.mutable_attributes()->mutable_timeseries(1)->set_ivalue(5);
        handler->HandleMessage("/device/" + externalId + "/attributes", changed.SerializeAsString());
        Require(cache->GetDrone(devices[0])->_Drone._state == 5, "Attributes use payload identity");
        auto changedHistory = timeseries[0];
        changedHistory.mutable_timeseries()->mutable_timeseries(0)->set_svalue("0");
        handler->HandleMessage("/device/" + externalId + "/timeseries", changedHistory.SerializeAsString());
        Require(cache->GetDrone(devices[0])->_Drone._position._posLat == 0, "Timeseries use payload identity");
        Require(cache->GetDrone(devices[0])->_TimeSeries._position.size() == 1, "Timeseries routed and replay deduplicated");
        const auto nameBeforeInvalidTopics = cache->GetDrone(devices[0])->_Drone._name;
        renamed.set_name("Unsupported route");
        for (auto const& topic : {std::string("/device"), std::string("/device/"), std::string("/device//attributes"),
                                 "/device/" + firstId + "/", "/device/" + firstId + "/attribute",
                                 "/device/" + firstId + "/attributes/extra", std::string("/device/+/attributes"),
                                 std::string("/status")}){
            handler->HandleMessage(topic, renamed.SerializeAsString());
        }
        Require(cache->GetDrone(devices[0])->_Drone._name == nameBeforeInvalidTopics, "Unsupported topic shapes ignored");
        Require(cache->GetDrone(devices[1])->_Drone._name == "Drone_2", "Other drone unchanged");
        handler->HandleMessage("/device/" + firstId, devices[0].SerializeAsString());
        handler->HandleMessage("/device/" + firstId + "/attributes", attributes[0].SerializeAsString());
        handler->HandleMessage("/device/" + firstId + "/timeseries", timeseries[0].SerializeAsString());

        // Valid telemetry cannot register an unknown drone.
        auto unknown = devices[0];
        unknown.set_id(NewDroneId());
        bool rejectedUnknown = false;
        try{ cache->StoreDevice(unknown); }catch (std::out_of_range const&){ rejectedUnknown = true; }
        Require(rejectedUnknown, "Unknown metadata rejected");
        auto unknownAttributes = attributes[0];
        *unknownAttributes.mutable_device() = unknown;
        rejectedUnknown = false;
        try{ cache->StoreAttribute(unknownAttributes); }catch (std::out_of_range const&){ rejectedUnknown = true; }
        Require(rejectedUnknown, "Unknown attributes rejected");
        auto unknownHistory = timeseries[0];
        *unknownHistory.mutable_device() = unknown;
        rejectedUnknown = false;
        try{ cache->StoreTimeseries(unknownHistory); }catch (std::out_of_range const&){ rejectedUnknown = true; }
        Require(rejectedUnknown && !cache->GetDrone(unknown), "Unknown history cannot create a drone");

        // Incomplete coordinate messages are rejected, not stored as pending.
        auto pair = timeseries[0];
        pair.mutable_device()->set_id(secondId);
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
        invalid.mutable_device()->set_id(NewDroneId());
        invalid.mutable_attributes()->mutable_timeseries(1)->set_svalue("wrong type");
        bool invalidRejected = false;
        try{ cache->StoreAttribute(invalid); }catch (std::invalid_argument const&){ invalidRejected = true; }
        Require(invalidRejected, "Reject invalid attribute batch");
        // Destruction joins the worker and completes the final flush.
        receiver = nullptr;
        handler.reset();
        cache.reset();
        Require(check.Scalar("SELECT count(*) FROM device") == 2, "Only registered drones are stored");
        Require(check.Scalar("SELECT count(*) FROM attributes WHERE device_id='" + firstId + "'") == 3, "Persist cached current state");
        Require(check.Scalar("SELECT count(*) FROM timeseries WHERE device_id='" + firstId + "'") == 2, "Persist simulator position pair once");
        Require(check.Scalar("SELECT count(*) FROM timeseries WHERE device_id='" + secondId + "' AND timestamp_ms=1700000000123") == 2, "Store coordinate batch at same timestamp");
        Require(check.Scalar("SELECT count(*) FROM timeseries WHERE device_id='" + secondId + "' AND name='latitude' AND string_value='1'") == 1, "Correct existing coordinate pair");
        Require(check.Scalar("SELECT count(*) FROM device WHERE id='" + invalid.device().id() + "'") == 0, "Bad batch cannot create partial drone");
        check.Execute("DELETE FROM timeseries; DELETE FROM attributes; DELETE FROM device; DELETE FROM client;");

        // Verify the new snapshot persistence API independently of the cache.
        Drone drone{};
        drone._droneId = devices[0].id();
        drone._clientId = devices[0].client_id().id();
        drone._name = devices[0].name();
        drone._online = online.bvalue();
        drone._state = state.ivalue();
        DroneTimeSeries droneHistory{};
        droneHistory._droneId = drone._droneId;
        droneHistory._clientId = drone._clientId;
        const auto stamp = std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{std::stoll(latitude.timestamp())}};
        TimeseriesPos position{stamp, std::stod(longitude.svalue()), std::stod(latitude.svalue())};
        drone._position = position;
        droneHistory._position.push_back(position);
        bool missingRejected = false;
        try{ db->StoreDrone(drone, droneHistory); }catch (std::out_of_range const&){ missingRejected = true; }
        Require(missingRejected && check.Scalar("SELECT count(*) FROM device") == 0,
                "Snapshot persistence cannot create a device");
        db->StoreDrone( drone, droneHistory, true );
        Require( check.Scalar("SELECT count(*) FROM device WHERE id='" + firstId + "' AND client_id=99") == 1, "Registered identity persistence" );
        Require( check.Scalar("SELECT count(*) FROM attributes WHERE device_id='" + firstId + "'") == 3, "Snapshot attribute count" );
        drone._online = true;
        drone._state = 5;
        position._posLat = 59;
        position._timestamp += std::chrono::milliseconds(1);
        drone._position = position;
        droneHistory._position.push_back(position);
        db->StoreDrone( drone, droneHistory );
        Require( check.Scalar("SELECT bool_value FROM attributes WHERE name='online'") == 1, "Update online" );
        Require( check.Scalar("SELECT uint32_value FROM attributes WHERE name='state'") == 5, "Update state" );
        Require( check.Scalar("SELECT latitude FROM attributes WHERE name='position'") == 59, "Update position" );
        Require( check.Scalar("SELECT count(*) FROM attributes") == 3, "Upsert must not duplicate attributes" );
        Require( check.Scalar("SELECT count(*) FROM timeseries") == 4, "Persist both coordinate pairs" );
        Require( droneHistory._position.size() == 2 && droneHistory._persistedPositions == 2, "Retain committed history" );
        db->StoreDrone(drone, droneHistory);
        Require( check.Scalar("SELECT count(*) FROM timeseries") == 4, "Repeated snapshot must not duplicate history" );
        Require( check.Scalar(("SELECT count(*) FROM timeseries WHERE timestamp_ms=" + std::to_string(stamp.time_since_epoch().count())).c_str()) == 2, "Exact timestamp" );

        auto replay = droneHistory;
        replay._persistedPositions = 0;
        replay._position[0]._posLat = 57;
        db->StoreDrone(drone, replay);
        Require(check.Scalar("SELECT count(*) FROM timeseries") == 4, "Database key deduplicates replay");

        // A SQL row appearing after cache startup must hydrate on a cache miss.
        {
            DroneCache reader(db);
            auto missing = devices[0];
            missing.set_id(externalId);
            Require(!reader.GetDrone(missing), "Missing drone stays absent");
            Drone external = drone;
            external._droneId = externalId;
            external._state = 42;
            auto externalHistory = droneHistory;
            externalHistory._droneId = externalId;
            externalHistory._persistedPositions = 0;
            db->StoreDrone(external, externalHistory, true);
            for (int attempt = 0; attempt < 100 && !reader.GetDrone(missing); ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            auto restored = reader.GetDrone(missing);
            Require(restored && restored->_Drone._state == 42, "Read miss loads SQL drone");
            Require(restored->_TimeSeries._position.size() == 2, "Read miss loads SQL history");
            missing.mutable_client_id()->set_id(123);
            Require(!reader.GetDrone(missing), "Read respects supplied client identity");
            Require(reader.ListDrones(99).size() == 2, "Client list includes SQL fallback");
            Require(reader.ListDrones(123).empty(), "Client list filters drones");
        }
        check.Execute("DELETE FROM timeseries WHERE device_id='" + externalId + "'; DELETE FROM attributes WHERE device_id='" + externalId + "'; DELETE FROM device WHERE id='" + externalId + "'");
        {
            DroneCache writer(db);
            Drone external = drone;
            external._droneId = externalId;
            external._state = 42;
            auto externalHistory = droneHistory;
            externalHistory._droneId = externalId;
            externalHistory._persistedPositions = 0;
            db->StoreDrone(external, externalHistory, true);
            auto metadata = devices[0];
            metadata.set_id(externalId);
            metadata.set_name("Late SQL drone");
            for (int attempt = 0; attempt < 100 && !writer.GetDrone(metadata); ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            writer.StoreDevice(metadata);
            const auto restored = writer.GetDrone(metadata);
            Require(restored && restored->_Drone._state == 42, "Write miss preserves SQL attributes");
            Require(restored->_TimeSeries._position.size() == 2, "Write miss preserves SQL history");
        }
        check.Execute("DELETE FROM timeseries WHERE device_id='" + externalId + "'; DELETE FROM attributes WHERE device_id='" + externalId + "'; DELETE FROM device WHERE id='" + externalId + "'");

        // Restart hydration must preserve values absent from incoming telemetry.
        auto loaded = db->LoadDrones();
        Require(loaded.size() == 1 && loaded[0].first._droneId == firstId && loaded[0].first._clientId == 99, "Restore drone identity");
        Require(loaded[0].first._online && loaded[0].first._state == 5, "Restore online and state");
        Require(loaded[0].first._position._posLat == 59 && loaded[0].first._position._timestamp == position._timestamp, "Restore current position independently of history");
        Require(loaded[0].second._position.size() == 2 && loaded[0].second._persistedPositions == 2, "Restore persisted history progress");
        Require(loaded[0].second._position[0]._timestamp == stamp && loaded[0].second._position[0]._posLat == 57, "Restore exact timestamp and corrected coordinate");
        {
            DroneCache restarted(db);
            auto metadata = devices[0];
            metadata.set_name("Restored");
            restarted.StoreDevice(metadata);
        }
        Require(check.Scalar("SELECT bool_value FROM attributes WHERE name='online'") == 1, "Metadata after restart preserves online");
        Require(check.Scalar("SELECT uint32_value FROM attributes WHERE name='state'") == 5, "Metadata after restart preserves state");
        Require(check.Scalar("SELECT count(*) FROM timeseries") == 4, "Hydrated history is not duplicated");
        {
            DroneCache restarted(db);
            auto correction = timeseries[0];
            // Correct an existing timestamp, without any online/state update.
            for (auto& value : *correction.mutable_timeseries()->mutable_timeseries()){
                value.set_timestamp(std::to_string(stamp.time_since_epoch().count()));
                value.set_svalue("1");
            }
            restarted.StoreTimeseries(correction);
        }
        Require(check.Scalar("SELECT bool_value FROM attributes WHERE name='online'") == 1, "Position after restart preserves online");
        Require(check.Scalar("SELECT uint32_value FROM attributes WHERE name='state'") == 5, "Position after restart preserves state");
        Require(check.Scalar("SELECT count(*) FROM timeseries") == 4, "Restored correction retains unique history");
        // Keep the existing rollback expectations independent of restart tests.
        check.Execute("UPDATE device SET name='Drone_1' WHERE id='" + firstId + "'");

        position._posLat = std::numeric_limits<double>::quiet_NaN();
        drone._position = position;
        droneHistory._position.push_back(position);
        bool rejected = false;
        try{ db->StoreDrone(drone, droneHistory); }catch (std::invalid_argument const&){ rejected = true; }
        Require( rejected, "Reject invalid coordinates" );
        droneHistory._position.back()._posLat = 58;
        drone._position._posLat = 58;
        drone._name = "Changed";
        check.Execute("CREATE TRIGGER reject_state BEFORE UPDATE ON attributes WHEN NEW.name='state' BEGIN SELECT RAISE(ABORT,'test'); END");
        rejected = false;
        try{ db->StoreDrone(drone, droneHistory); }catch (std::runtime_error const&){ rejected = true; }
        Require( rejected, "Propagate database failure" );
        Require( droneHistory._position.size() == 3 && droneHistory._persistedPositions == 2, "Keep history and progress after rollback" );
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
