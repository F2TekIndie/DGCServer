#include "mqsubscriptions.h"
#include "drone.h"
#include "mqtt.h"
#include "attributes.pb.h"
#include "timeseries.pb.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

// Replace the network boundary, keeping simulator serialization and receiver
// deserialization/storage unchanged for an end-to-end payload contract check.
namespace { dgcSubscriptions* receiver = nullptr; }
mqClient::mqClient(Configuration const& config) : _config(config), _client(_ioc) {}
mqClient::~mqClient() {}
void mqClient::publish(std::string const& topic, std::string const& payload) {
    receiver->HandleMessage(topic, payload);
}

namespace {
    void Require(bool condition) {
        if (!condition) throw std::runtime_error("Persistence check failed");
    }
    struct TestFile {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("dgc-persistence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
        ~TestFile() { std::error_code ec; std::filesystem::remove(path, ec); }
    };
    struct Connection {
        sqlite3* db = nullptr;
        explicit Connection(std::filesystem::path const& path) {
            Require(sqlite3_open(path.string().c_str(), &db) == SQLITE_OK);
        }
        ~Connection() { sqlite3_close(db); }
        void Execute(std::string const& sql) {
            if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(db));
        }
        sqlite3_int64 Scalar(char const* sql) {
            sqlite3_stmt* statement = nullptr;
            Require(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK);
            const int rc = sqlite3_step(statement);
            const auto result = sqlite3_column_int64(statement, 0);
            sqlite3_finalize(statement);
            Require(rc == SQLITE_ROW);
            return result;
        }
    };
    template<class Message>
    void Send(dgcSubscriptions& target, std::string const& topic, Message const& message) {
        std::string payload;
        Require(message.SerializeToString(&payload));
        target.HandleMessage(topic, payload);
    }
}

int main(int argc, char** argv) {
    try {
        Require(argc == 2);
        TestFile file;
        Connection check(file.path);
        std::ifstream schema(argv[1]);
        Require(schema.good());
        check.Execute(std::string(std::istreambuf_iterator<char>(schema), {}));
        Configuration config;
        config._dbName = file.path.string();
        auto db = std::make_shared<database>(config);
        dgcSubscriptions handler(db);
        receiver = &handler;

        // Telemetry before registration creates the client and device.
        dronegroundcontrol::AttributeRequest attribute;
        attribute.mutable_device()->set_id(42);
        attribute.mutable_device()->mutable_client_id()->set_id(99);
        attribute.mutable_attrib()->set_name("online");
        attribute.mutable_attrib()->set_bvalue(false);
        Send(handler, dronesdk::mqtt::EP_ATTRIBUTES, attribute);
        Require(check.Scalar("SELECT count(*) FROM device WHERE id=42 AND client_id=99") == 1);
        Require(check.Scalar("SELECT count(*) FROM attributes WHERE device_id=42 AND bool_value=0") == 1);

        // Changing oneof type clears old columns; SQL-looking text is data.
        attribute.mutable_attrib()->set_svalue("'); DROP TABLE device; --");
        Send(handler, dronesdk::mqtt::EP_ATTRIBUTES, attribute);
        Require(check.Scalar("SELECT count(*) FROM attributes WHERE device_id=42 AND bool_value IS NULL AND value_type='string'") == 1);
        attribute.mutable_attrib()->set_ivalue(4294967295u);
        Send(handler, dronesdk::mqtt::EP_ATTRIBUTES, attribute);
        Require(check.Scalar("SELECT uint32_value FROM attributes WHERE device_id=42") == 4294967295LL);
        attribute.mutable_attrib()->mutable_pos()->set_lat("58.08");
        attribute.mutable_attrib()->mutable_pos()->set_long_("10.08");
        Send(handler, dronesdk::mqtt::EP_ATTRIBUTES, attribute);
        Require(check.Scalar("SELECT count(*) FROM attributes WHERE value_type='position' AND uint32_value IS NULL") == 1);

        // Invalid positions roll back the automatic identity insertion too.
        attribute.mutable_device()->set_id(43);
        attribute.mutable_attrib()->mutable_pos()->set_lat("91");
        Send(handler, dronesdk::mqtt::EP_ATTRIBUTES, attribute);
        Require(check.Scalar("SELECT count(*) FROM device WHERE id=43") == 0);

        dronegroundcontrol::TimeseriesRequest sample;
        *sample.mutable_device() = attribute.device();
        sample.mutable_device()->set_id(44);
        sample.mutable_data()->set_name("latitude");
        sample.mutable_data()->set_timestamp("1700000000123");
        sample.mutable_data()->set_svalue("58.08");
        Send(handler, dronesdk::mqtt::EP_TIMESERIES, sample);
        Send(handler, dronesdk::mqtt::EP_TIMESERIES, sample);
        Require(check.Scalar("SELECT count(*) FROM timeseries WHERE device_id=44 AND timestamp_ms=1700000000123") == 2);
        sample.mutable_data()->set_bvalue(true);
        Send(handler, dronesdk::mqtt::EP_TIMESERIES, sample);
        sample.mutable_data()->set_ivalue(4294967295u);
        Send(handler, dronesdk::mqtt::EP_TIMESERIES, sample);
        sample.mutable_device()->set_id(45);
        sample.mutable_data()->set_timestamp("123junk");
        Send(handler, dronesdk::mqtt::EP_TIMESERIES, sample);
        Require(check.Scalar("SELECT count(*) FROM device WHERE id=45") == 0);

        handler.HandleMessage(dronesdk::mqtt::EP_DEVICE, std::string(1, '\xff'));
        handler.HandleMessage(dronesdk::mqtt::EP_DEVICE, "");
        handler.HandleMessage("/unknown", "ignored");

        // Run the actual simulator's initial five messages through the handlers.
        mqClient mqtt(config);
        DroneManager simulator(mqtt);
        simulator.Init(1);
        Require(check.Scalar("SELECT count(*) FROM device WHERE id=1 AND name='Drone_1'") == 1);
        Require(check.Scalar("SELECT count(*) FROM attributes WHERE device_id=1") == 2);
        Require(check.Scalar("SELECT count(*) FROM timeseries WHERE device_id=1") == 2);
        Require(check.Scalar("SELECT count(*) FROM pragma_foreign_key_check") == 0);
        std::cout << "MQTT protobuf persistence checks passed\n";
    }
    catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
