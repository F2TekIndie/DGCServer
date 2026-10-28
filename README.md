# DGCServer

**Status: proof-of-concept prototype.** The simulator publishes telemetry over
MQTT and the server persists it in SQLite. gRPC application operations and
desktop telemetry integration remain incomplete. This is not production-ready.

## Current behavior

| Executable | Current behavior |
| --- | --- |
| `dgcserver` | Opens SQLite, starts MQTT subscription processing and a gRPC server, then waits for Ctrl+C (SIGINT). |
| `dgcsim` | Simulates drones, publishes Protobuf identity/attributes/position samples over MQTT, and runs until Ctrl+C. |

The gRPC service methods currently return OK with default/empty responses and
perform no database operations. MQTT device, attribute, and timeseries payloads
are deserialized and stored in SQLite. Unknown clients/devices are created on
first contact; registration is not required. The database wrapper enables foreign
keys, but does not create tables or expose application read queries.

## Repository layout

Keep these repositories alongside each other:

```text
DroneGroundControl/
├── DGC/
├── DGCServer/
└── DroneSDK/
```

- [DGC](../DGC/README.md): Qt desktop video/map application.
- [DroneSDK](../DroneSDK/README.md): Protobuf messages and gRPC bindings.

## Dependencies

- CMake 3.21 or later and a C++23 compiler with `std::jthread` support.
- Boost.System, Boost.Asio, and Boost.MQTT5 headers. The Boost installation must
  include `boost/mqtt5/mqtt_client.hpp` and `boost/asio/as_tuple.hpp`.
- Threading support.
- Protobuf compiler and development library.
- gRPC C++ development files, `gRPCConfig.cmake`, and `grpc_cpp_plugin`.
- SQLite3 development library; the `sqlite3` CLI for manual schema creation.
- An MQTT broker for messaging; the application does not launch one.

Qt and GStreamer are not required for this project. If CMake cannot find gRPC,
check that its development package is installed, not only its runtime/plugins.
For a custom installation, set `CMAKE_PREFIX_PATH` or `gRPC_DIR`.

## Build

From DGCServer:

```sh
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Debug
cmake --build build/local --parallel
```

CMake builds the sibling `DroneSDK/protocol` target and generates both Protobuf
message code and gRPC bindings. Both executables link `DGCProto`; only the server
links SQLite, as does the persistence test. The former SDK `sources/` directory
is no longer included. CTest enables the persistence test through `BUILD_TESTING`.

## Initialize and run

Create the schema once in an empty database, then run from the same directory:

```sh
sqlite3 -bail dgc.sqlite < sql/create_schema.sql
./build/local/dgcserver
```

Press Ctrl+C to request server shutdown. Run the simulator separately:

```sh
./build/local/dgcsim 100
```

The simulator requires a drone count of at least 100. It publishes initial
identity, attributes, and position for each drone, then updates randomized state
and movement approximately every 10 seconds using measured elapsed time. Press
Ctrl+C to stop it; shutdown may wait for the current sleep interval.

See [database documentation](sql/README.md) for relationships and value formats.
Opening SQLite can create an empty file; it does not apply the SQL schema.

## Configuration and endpoints

`loadConfiguration("server.ini")` currently ignores the filename and returns
hard-coded defaults from `sdk/config.h`. Creating an INI file has no effect.

| Setting | Default |
| --- | --- |
| SQLite file | `dgc.sqlite`, relative to the process working directory |
| MQTT broker | `127.0.0.1:1883` |
| gRPC listener | `127.0.0.1:5600`, insecure credentials |
| MQTT diagnostics | `_dbgLogging = true`: Boost.MQTT5 debug logger |

The gRPC TCP port is separate from the desktop receiver's UDP video port 5600.
The server subscribes to these exact MQTT topics, including the leading slash:

- `/device`
- `/status`
- `/device/attribute`
- `/device/timeseries`

No wildcard matching is configured. Simulator payloads are binary Protobuf:
`Device` on `/device`, `AttributeRequest.attributes` on `/device/attribute`
(containing `online` and `state`), and `TimeseriesRequest.timeseries` on
`/device/timeseries` (containing `latitude` and `longitude`). Each group is one
MQTT publish per drone, rather than one publish per value. Every request includes device/client identity. Coordinates
use string values with round-trip double precision, and timestamp strings are
UTC Unix milliseconds. Both coordinate samples share one timestamp. Initial
values are sent for every drone; later position samples are sent for traveling
drones. Server handlers deserialize these messages and store them transactionally.

Publishing and all four subscriptions use QoS 2 (`exactly_once`). The publish
completion handler receives an error code, broker reason code, and PUBCOMP
properties. Subscription QoS remains subject to broker approval and publisher
QoS; it cannot upgrade a lower-QoS publisher.

Publishing uses RETAIN. Retained storage holds only the latest payload for each
exact topic, not one per drone or attribute. A newly started server cannot
recover all drone state from these shared retained topics. QoS 2 governs MQTT
protocol delivery; it does not provide exactly-once SQLite transactions or an
acknowledgment that the database write succeeded.

## Implementation and limitations

- `sdk/mqclient.*`: MQTT wrapper; one worker runs its Asio context. Publish,
  disconnect, and subscription setup are posted to that context. Connect is
  currently called before starting the worker.
- `sdk/config.h`, `sdk/mqtt.h`: default settings and topic constants.
- `server/db.*`: SQLite connection ownership, foreign keys, and transactional writes.
- `server/grpc.*`: service stubs, listener, and waiting worker.
- `server/mqsubscriptions.*`: topic routing, Protobuf decoding, and persistence.
- `sim/drone.*`: randomized state/movement updates and Protobuf publishing.
- `sql/create_schema.sql`: initial database schema.

Shutdown currently posts MQTT disconnect and immediately stops its context, so
protocol-level disconnect completion is not guaranteed. Subscription handling
logs every result but decides subscription success from only the first result.
Exceptions escaping MQTT callbacks can terminate the worker/process. gRPC startup failure is reported, but the entry point still
returns zero. Configuration parsing, authentication, RPC database operations,
and live-broker end-to-end tests remain future work.

## MQTT persistence

Run `sql/create_schema.sql` before starting the server. Each device/telemetry
message upserts its client and device, generating `Client_<id>` / `Device_<id>`
names if omitted. Nonempty identity metadata updates existing records; IDs must
be nonzero. Device IDs are global in the current schema. Attributes replace the
current `(device_id, name)` value; timeseries messages append samples, including
repeated timestamps. Coordinates in position attributes are converted to numeric
degrees; latitude/longitude timeseries values remain strings as sent.

Each message/batch is an atomic transaction with bound SQL parameters. A bad
item rolls back the entire batch. Legacy single-value `attrib` and `data`
payloads remain accepted; requests must contain either a single value or a
nonempty batch, never both. Rebuild simulator and server together after the
protocol update. Invalid
Protobuf, missing values, invalid coordinates/timestamps, or database errors are
logged and the message is rejected without stopping reception. `/status` has no
payload contract yet and is logged as unsupported. Replayed retained messages,
application resends, or separate publish operations can append duplicate
historical samples; no database deduplication is implemented.

Run the payload-to-database integration test (no broker required):

```sh
cmake -S . -B build/local -DBUILD_TESTING=ON
cmake --build build/local --parallel
ctest --test-dir build/local --output-on-failure
```

## Diagnostics and load

Boost.MQTT5 debug output reports DNS/TCP connection attempts and MQTT handshake
results. Set `_dbgLogging` to false in `sdk/config.h` to select error-level
logging. `async_run` still uses `detached`, so its final completion is not
reported through an application callback.

SQLite writes run synchronously in the MQTT receive handler. There is no
application queue-depth counter or throughput/latency reporting yet. Broker
statistics alone do not measure how many messages are waiting for database
storage.

For a broker you run manually, enable verbose output with `mosquitto -v`.
Do not start a second broker on an occupied port. For an existing broker, use
its logging configuration (`log_dest stderr`, `log_type all`) and view its
configured output destination. If managed by systemd with stderr logging:

```sh
journalctl -u mosquitto -f
```

Watch broker statistics separately, if allowed by its access controls:

```sh
mosquitto_sub -h 127.0.0.1 -v -t '$SYS/broker/#'
```

Stored-message totals include retained data and queues across clients; they are
not a per-DGCServer outstanding-message count. Verbose logs can affect load
measurements.
