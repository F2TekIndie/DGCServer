#include "grpc.h"
#include "dronecache.h"
#include "droneidentity.h"
#include "db.h"
#include "config.h"
#include "devices.grpc.pb.h"
#include "attributes.grpc.pb.h"
#include "timeseries.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
    void Require(bool value, char const* message){
        if (!value) throw std::runtime_error(message);
    }
    struct TestDatabase {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("dgc-rpc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
        sqlite3* connection = nullptr;
        TestDatabase(){ Require(sqlite3_open(path.c_str(), &connection) == SQLITE_OK, "Open SQLite"); }
        ~TestDatabase(){ sqlite3_close(connection); std::error_code error; std::filesystem::remove(path, error); }
        void Execute(std::string const& sql){
            if (sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(connection));
        }
        int Count(std::string const& sql){
            sqlite3_stmt* statement = nullptr;
            Require(sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK, "Prepare count");
            int result = sqlite3_step(statement);
            int value = sqlite3_column_int(statement, 0);
            sqlite3_finalize(statement);
            Require(result == SQLITE_ROW, "Read count");
            return value;
        }
    };
    template<class Stub, class Method, class Request, class Response>
    grpc::Status Call(Stub& stub, Method method, Request const& request, Response& response){
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
        return (stub.get()->*method)(&context, request, &response);
    }
}

int main(int argc, char** argv){
    try {
        Require(argc == 2, "Schema argument required");
        TestDatabase file;
        std::ifstream schema(argv[1]);
        Require(schema.good(), "Read schema");
        file.Execute(std::string(std::istreambuf_iterator<char>(schema), {}));
        Configuration config;
        config._dbName = file.path.string();
        config._grpcServer = {"127.0.0.1", 0};
        auto db = std::make_shared<database>(config);
        auto cache = std::make_shared<DroneCache>(db);
        namespace proto = dronegroundcontrol;
        std::string deletedId;
        {
            rpcServer server(config, cache);
            Require(server.listen(), "Start RPC server");
            server.start();
            auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(server.listeningPort()), grpc::InsecureChannelCredentials());
            auto devices = proto::DeviceFunctions::NewStub(channel);
            auto attributes = proto::AttributeFunctions::NewStub(channel);
            auto history = proto::TimeseriesFunctions::NewStub(channel);
            proto::Client client;
            client.set_id(99);
            file.Execute("BEGIN EXCLUSIVE"); // RPCs must succeed even while SQL is unavailable.
            proto::Device drone;
            Require(Call(devices, &proto::DeviceFunctions::Stub::CreateDevice, client, drone).ok(), "Create RPC");
            deletedId = drone.id();
            Require(ValidDroneId(drone.id()) && drone.name() == "Device_" + drone.id() && drone.client_id().id() == 99, "Allocated identity");
            proto::Device second;
            Require(Call(devices, &proto::DeviceFunctions::Stub::CreateDevice, client, second).ok() && second.id() != drone.id(), "Unique allocated IDs");
            proto::Devices list;
            Require(Call(devices, &proto::DeviceFunctions::Stub::ListDevices, client, list).ok() && list.devicelist_size() == 2, "List RPC");
            proto::Client invalidClient;
            Require(Call(devices, &proto::DeviceFunctions::Stub::CreateDevice, invalidClient, second).error_code() == grpc::StatusCode::INVALID_ARGUMENT, "Reject invalid client");

            proto::AttributeRequest update;
            *update.mutable_device() = drone;
            update.mutable_attrib()->set_name("state");
            update.mutable_attrib()->set_ivalue(42);
            proto::AttributeStatusReply attributeStatus;
            Require(Call(attributes, &proto::AttributeFunctions::Stub::UpdateAttribute, update, attributeStatus).ok() && attributeStatus.success(), "Update attribute RPC");
            proto::AttributeQuery query;
            *query.mutable_device() = drone;
            query.set_name("state");
            proto::Attribute attribute;
            Require(Call(attributes, &proto::AttributeFunctions::Stub::GetAttribute, query, attribute).ok() && attribute.ivalue() == 42, "Get named attribute RPC");
            query.set_name("unknown");
            Require(Call(attributes, &proto::AttributeFunctions::Stub::GetAttribute, query, attribute).error_code() == grpc::StatusCode::INVALID_ARGUMENT, "Reject unknown attribute");
            proto::Attributes all;
            Require(Call(attributes, &proto::AttributeFunctions::Stub::GetAttributes, drone, all).ok() && all.timeseries_size() == 3, "Get all attributes RPC");
            update.mutable_attrib()->set_svalue("bad");
            Require(Call(attributes, &proto::AttributeFunctions::Stub::UpdateAttribute, update, attributeStatus).error_code() == grpc::StatusCode::INVALID_ARGUMENT, "Reject wrong attribute type");

            proto::PositionRequest position;
            *position.mutable_device() = drone;
            position.mutable_pos()->set_lat("52.1234567890123");
            position.mutable_pos()->set_long_("13.9876543210987");
            Require(Call(attributes, &proto::AttributeFunctions::Stub::UpdatePosition, position, attributeStatus).ok(), "Update position RPC");
            proto::Position current;
            Require(Call(attributes, &proto::AttributeFunctions::Stub::GetPosition, drone, current).ok() && std::stod(current.lat()) == std::stod(position.pos().lat()), "Read position precision");

            proto::TimeseriesRequest batch;
            *batch.mutable_device() = drone;
            // Deliberately send history out of order, including timestamp zero.
            for (auto stamp : {2000, 0, 1000}){
                for (auto name : {"latitude", "longitude"}){
                    auto* sample = batch.mutable_timeseries()->add_timeseries();
                    sample->set_name(name);
                    sample->set_timestamp(std::to_string(stamp));
                    sample->set_svalue(stamp == 1000 ? "1.25" : "2.5");
                }
            }
            proto::TimeseriesStatusReply historyStatus;
            Require(Call(history, &proto::TimeseriesFunctions::Stub::AddTimeseries, batch, historyStatus).ok() && historyStatus.success(), "Add history RPC");
            proto::TimeseriesQuery sampleQuery;
            *sampleQuery.mutable_device() = drone;
            sampleQuery.set_name("latitude");
            sampleQuery.set_timestamp_ms(0);
            proto::TimeSerie sample;
            Require(Call(history, &proto::TimeseriesFunctions::Stub::GetTimeseries, sampleQuery, sample).ok() && sample.timestamp() == "0", "Exact epoch timestamp");
            sampleQuery.set_timestamp_ms(1000);
            Require(Call(history, &proto::TimeseriesFunctions::Stub::GetTimeseries, sampleQuery, sample).ok() && sample.svalue() == "1.25", "Exact named sample");
            sampleQuery.set_timestamp_ms(999);
            Require(Call(history, &proto::TimeseriesFunctions::Stub::GetTimeseries, sampleQuery, sample).error_code() == grpc::StatusCode::NOT_FOUND, "Missing sample");
            sampleQuery.clear_timestamp_ms();
            Require(Call(history, &proto::TimeseriesFunctions::Stub::GetTimeseries, sampleQuery, sample).ok() && std::stoll(sample.timestamp()) > 2000, "Latest sample includes position updates");
            proto::TimeseriesRangeQuery rangeQuery;
            *rangeQuery.mutable_device() = drone;
            rangeQuery.set_start_timestamp_ms(0);
            rangeQuery.set_end_timestamp_ms(2000);
            proto::TimeSeries range;
            Require(Call(history, &proto::TimeseriesFunctions::Stub::GetTimeseriesRange, rangeQuery, range).ok() && range.timeseries_size() == 6 && range.range() == 6, "Inclusive range with both series");
            Require(range.timeseries(0).timestamp() == "0" && range.timeseries(4).timestamp() == "2000", "Range sorted by timestamp");
            rangeQuery.set_name("longitude");
            Require(Call(history, &proto::TimeseriesFunctions::Stub::GetTimeseriesRange, rangeQuery, range).ok() && range.timeseries_size() == 3, "Named range");
            rangeQuery.set_start_timestamp_ms(2001);
            Require(Call(history, &proto::TimeseriesFunctions::Stub::GetTimeseriesRange, rangeQuery, range).error_code() == grpc::StatusCode::INVALID_ARGUMENT, "Reject reversed range");
            rangeQuery.set_end_timestamp_ms(2002);
            Require(Call(history, &proto::TimeseriesFunctions::Stub::GetTimeseriesRange, rangeQuery, range).ok() && range.timeseries_size() == 0, "Empty range");

            auto unknownUpdate = update;
            unknownUpdate.mutable_device()->set_id("11111111-1111-4111-8111-111111111111");
            unknownUpdate.mutable_attrib()->set_ivalue(1);
            Require(Call(attributes, &proto::AttributeFunctions::Stub::UpdateAttribute, unknownUpdate, attributeStatus).error_code() == grpc::StatusCode::NOT_FOUND, "Unknown attribute update rejected");
            auto unknownPosition = position;
            unknownPosition.mutable_device()->set_id(unknownUpdate.device().id());
            Require(Call(attributes, &proto::AttributeFunctions::Stub::UpdatePosition, unknownPosition, attributeStatus).error_code() == grpc::StatusCode::NOT_FOUND, "Unknown position update rejected");
            auto unknownBatch = batch;
            unknownBatch.mutable_device()->set_id(unknownUpdate.device().id());
            Require(Call(history, &proto::TimeseriesFunctions::Stub::AddTimeseries, unknownBatch, historyStatus).error_code() == grpc::StatusCode::NOT_FOUND, "Unknown history update rejected");

            for (auto const& badName : {std::string("   "), std::string("\t\n "), std::string("bad\0name", 8)}){
                auto invalidName = update;
                invalidName.mutable_device()->set_name(badName);
                invalidName.mutable_attrib()->set_ivalue(88);
                Require(Call(attributes, &proto::AttributeFunctions::Stub::UpdateAttribute, invalidName, attributeStatus).error_code() == grpc::StatusCode::INVALID_ARGUMENT, "Reject invalid name before changing attributes");
                auto metadata = drone;
                metadata.set_name(badName);
                bool rejectedName = false;
                try{ cache->StoreDevice(metadata); }catch (std::invalid_argument const&){ rejectedName = true; }
                Require(rejectedName, "Reject invalid MQTT metadata name");
                Require(cache->GetDrone(drone)->_Drone._state == 42 && cache->GetDrone(drone)->_Drone._name == drone.name(), "Invalid name leaves cache unchanged");
            }
            Require(file.Count("SELECT count(*) FROM device") == 0, "RPC creation and updates never write SQL directly");
            file.Execute("COMMIT");

            // A failed initial snapshot rolls back identity and is retried by the worker.
            file.Execute("CREATE TRIGGER reject_creation BEFORE INSERT ON attributes WHEN NEW.name='state' BEGIN SELECT RAISE(ABORT,'test'); END");
            proto::Device retryDrone;
            Require(Call(devices, &proto::DeviceFunctions::Stub::CreateDevice, client, retryDrone).ok(), "Creation accepted independently of SQL failure");
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            Require(file.Count("SELECT count(*) FROM device WHERE id='" + retryDrone.id() + "'") == 0, "Failed creation snapshot rolls back identity");
            Require(cache->GetDrone(retryDrone).has_value(), "Failed snapshot retains cached drone");
            file.Execute("DROP TRIGGER reject_creation");
            for (int attempt = 0; attempt < 100 && file.Count("SELECT count(*) FROM device WHERE id='" + retryDrone.id() + "'") == 0; ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            Require(file.Count("SELECT count(*) FROM device WHERE id='" + retryDrone.id() + "'") == 1, "Worker retries creation");

            // Introduce a drone after cache construction to exercise SQL fallback via RPC.
            Drone external{};
            external._droneId = NewDroneId();
            external._clientId = 99;
            external._name = "SQL drone";
            external._state = 7;
            DroneTimeSeries externalHistory{};
            externalHistory._droneId = external._droneId;
            externalHistory._clientId = 99;
            db->StoreDrone(external, externalHistory, true);
            proto::Device sqlDrone;
            sqlDrone.set_id(external._droneId);
            query.set_name("state");
            *query.mutable_device() = sqlDrone;
            for (int attempt = 0; attempt < 100 && !cache->GetDrone(sqlDrone); ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            Require(Call(attributes, &proto::AttributeFunctions::Stub::GetAttribute, query, attribute).ok() && attribute.ivalue() == 7, "Worker imports SQL drone");
            proto::Device allocated;
            Require(Call(devices, &proto::DeviceFunctions::Stub::CreateDevice, client, allocated).ok() && ValidDroneId(allocated.id()) && allocated.id() != external._droneId, "Allocation respects SQL IDs");

            proto::DeviceStatusReply deletion;
            auto wrongClient = drone;
            wrongClient.mutable_client_id()->set_id(100);
            Require(Call(devices, &proto::DeviceFunctions::Stub::DeletDevice, wrongClient, deletion).error_code() == grpc::StatusCode::NOT_FOUND, "Delete respects client identity");
            // Persist first, then make SQL deletion fail. Cache deletion must still succeed.
            for (int attempt = 0; attempt < 100 && file.Count("SELECT count(*) FROM device WHERE id='" + drone.id() + "'") == 0; ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            file.Execute("CREATE TRIGGER reject_delete BEFORE DELETE ON device BEGIN SELECT RAISE(ABORT,'test'); END");
            Require(Call(devices, &proto::DeviceFunctions::Stub::DeletDevice, drone, deletion).ok() && deletion.success(), "Delete RPC");
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            Require(file.Count("SELECT count(*) FROM device WHERE id='" + drone.id() + "'") == 1, "Failed SQL deletion stays pending");
            Require(!cache->GetDrone(drone), "Tombstone prevents worker from reimporting deleted drone");
            file.Execute("DROP TRIGGER reject_delete");
            proto::Device replacement;
            Require(Call(devices, &proto::DeviceFunctions::Stub::CreateDevice, client, replacement).ok() && replacement.id() != drone.id(), "Replacement gets a fresh UUID");
            Require(Call(attributes, &proto::AttributeFunctions::Stub::GetPosition, drone, current).error_code() == grpc::StatusCode::NOT_FOUND, "Deleted cache entry absent");
            update.mutable_attrib()->set_ivalue(1);
            Require(Call(attributes, &proto::AttributeFunctions::Stub::UpdateAttribute, update, attributeStatus).error_code() == grpc::StatusCode::NOT_FOUND, "Updates cannot recreate deleted drones");
            Require(Call(devices, &proto::DeviceFunctions::Stub::DeletDevice, drone, deletion).error_code() == grpc::StatusCode::NOT_FOUND, "Repeated delete reports missing drone");
        }
        cache.reset(); // Flush remaining snapshots; deleted drone must not reappear.
        Require(file.Count("SELECT count(*) FROM device WHERE id='" + deletedId + "'") == 0, "Deleted drone stays absent after flush");
        Require(file.Count("SELECT count(*) FROM attributes WHERE device_id='" + deletedId + "'") == 0, "Deleted attributes absent");
        Require(file.Count("SELECT count(*) FROM timeseries WHERE device_id='" + deletedId + "'") == 0, "Deleted history absent");
        DroneCache restarted(db);
        proto::Device deleted;
        deleted.set_id(deletedId);
        Require(!restarted.GetDrone(deleted), "Deleted drone stays absent after restart");
        Require(file.Count("SELECT count(*) FROM pragma_foreign_key_check") == 0, "Foreign key integrity");
        std::cout << "RPC cache integration checks passed\n";
    }
    catch (std::exception const& error){ std::cerr << error.what() << '\n'; return 1; }
}
