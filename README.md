# DGCServer

**Status: proof-of-concept prototype.** Networking lifecycle and database
scaffolding exist, but request processing, persistence, and simulation remain
incomplete. This is not production-ready.

## Current behavior

| Executable | Current behavior |
| --- | --- |
| `dgcserver` | Opens SQLite, starts MQTT subscription processing and a gRPC server, then waits for Ctrl+C (SIGINT). |
| `dgcsim` | Starts an MQTT client, creates an empty drone manager, then shuts down; no simulation loop or publishing is implemented. |

The gRPC service methods currently return OK with default/empty responses and
perform no database operations. MQTT messages reach `HandleMessage`, whose body
is still empty. The database wrapper opens a connection and enables foreign
keys, but does not create tables or expose application queries.

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
links SQLite. The former SDK `sources/` directory is no longer included.

## Initialize and run

Create the schema once in an empty database, then run from the same directory:

```sh
sqlite3 -bail dgc.sqlite < sql/create_schema.sql
./build/local/dgcserver
```

Press Ctrl+C to request server shutdown. Run the simulator separately:

```sh
./build/local/dgcsim
```

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

The gRPC TCP port is separate from the desktop receiver's UDP video port 5600.
The server subscribes to these exact MQTT topics, including the leading slash:

- `/device`
- `/status`
- `/device/attribute`
- `/device/timeseries`

No wildcard matching is configured. Payload encoding and database mapping are
not implemented. Publish requests currently use QoS 0 with RETAIN enabled;
subscriptions request QoS 2, subject to broker approval and publisher QoS.

## Implementation and limitations

- `sdk/mqclient.*`: MQTT wrapper; one worker runs its Asio context. Publish,
  disconnect, and subscription setup are posted to that context. Connect is
  currently called before starting the worker.
- `sdk/config.h`, `sdk/mqtt.h`: default settings and topic constants.
- `server/db.*`: SQLite connection ownership and foreign-key setup.
- `server/grpc.*`: service stubs, listener, and waiting worker.
- `server/mqsubscriptions.*`: subscriptions and placeholder message handler.
- `sim/drone.*`: drone state containers and empty simulation methods.
- `sql/create_schema.sql`: initial database schema.

Shutdown currently posts MQTT disconnect and immediately stops its context, so
protocol-level disconnect completion is not guaranteed. Subscription handling
checks only the first result, and exceptions escaping MQTT handlers can terminate
the worker/process. gRPC startup failure is reported, but the entry point still
returns zero. Configuration parsing, authentication, RPC/database behavior, and
end-to-end tests remain future work.
