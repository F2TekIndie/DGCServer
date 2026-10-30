# DGCServer

Proof-of-concept server and simulator for Drone Ground Control. The server
accepts gRPC requests and MQTT telemetry through an in-memory drone cache. Its
DB worker synchronizes that cache with SQLite.

## Projects and data flow

Keep the repositories alongside each other:

```text
DroneGroundControl/
├── DGC/          Qt desktop application
├── DGCServer/    Server, simulator, and SQL schema
└── DroneSDK/     Shared Protobuf messages and gRPC bindings
```

```text
DGC → gRPC → server services → DroneCache ↔ DB worker ↔ SQLite
                                  ↑
dgcsim → MQTT broker → subscriptions
```

- [DGC](../DGC/README.md) accesses the server through its `gRPCClient` class.
- [DroneSDK](../DroneSDK/README.md) defines the messages and RPC interfaces.
- [Database documentation](sql/README.md) describes storage and constraints.

MQTT and gRPC handlers read or update cached state only. They do not query SQL
or wait for persistence. Only the DB worker calls the database read/write APIs.
The standalone SQL seeding script is separate from the server request paths.

## Dependencies

- CMake 3.21 or later and a C++23 compiler.
- Boost.System, Boost.Asio, Boost.UUID, and Boost.MQTT5 headers, including
  `boost/mqtt5/mqtt_client.hpp` and `boost/asio/as_tuple.hpp`.
- Threading support.
- Protobuf compiler and development library.
- gRPC C++ development files, `gRPCConfig.cmake`, and `grpc_cpp_plugin`.
- SQLite3 development library; the `sqlite3` CLI for schema initialization.
- Python 3 for the optional drone-creation script.
- An MQTT broker for running the server/simulator workflow.

Qt and GStreamer are not required. For custom library installations, configure
`CMAKE_PREFIX_PATH` or the relevant package directory, such as `gRPC_DIR`.

## Build and test

Run from DGCServer:

```sh
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/local --parallel
ctest --test-dir build/local --output-on-failure
```

The build generates the sibling SDK's `DGCProto` library and creates:

| Target | Purpose |
| --- | --- |
| `dgcserver` | gRPC services, MQTT subscriptions, cache, and SQLite synchronization. |
| `dgcsim` | Discover registered drones through gRPC and publish simulated telemetry over MQTT. |
| `grpc_cache_test` | Exercise RPCs through a local gRPC listener and temporary SQLite database. |
| `mqtt_persistence_test` | Exercise simulator payloads, subscription routing, cache, and persistence with the MQTT network boundary replaced. |

Tests require no MQTT broker. `grpc_cache_test` requires permission to open a
loopback listener. Set `BUILD_TESTING=OFF` to omit the test targets.

## Initialize and run

Run the schema once against a new, empty development database. Seed at least
100 drones for the simulator's client, which is client 99:

```sh
sqlite3 -bail dgc.sqlite < sql/create_schema.sql
python3 scripts/create_drones.py 100 --db dgc.sqlite
```

The Python script opens an existing database, generates UUID v4 IDs, and creates
`Device_<uuid>` names in one transaction. It creates the client if needed. Use
`--client-id` to seed another client:

```sh
python3 scripts/create_drones.py 10 --db dgc.sqlite --client-id 7
```

Start an MQTT broker separately, then run the server from the directory
containing `dgc.sqlite`:

```sh
./build/local/dgcserver
```

In another terminal, run the simulator:

```sh
./build/local/dgcsim
```

The simulator does not take a drone-count argument. It calls `ListDevices` for
client 99, requires at least 100 returned drones, and uses all returned UUIDs.
It publishes initial metadata, online/state attributes, and position samples,
then updates simulated state and movement approximately every two seconds using
measured elapsed time. Start the server before the simulator. Stop each process
with Ctrl+C; the simulator can wait for its current sleep interval to finish.

The server opens SQLite but does not create tables. Use the current schema for
development databases. SQL-created drones
are imported by the worker's discovery cycle if the server is already running.

## Configuration and endpoints

`loadConfiguration("server.ini")` currently ignores the filename and returns
source defaults from `sdk/config.h`. Creating an INI file has no effect.

| Setting | Default |
| --- | --- |
| SQLite file | `dgc.sqlite`, relative to the process working directory. |
| MQTT broker | `127.0.0.1:1883`. |
| gRPC listener | `127.0.0.1:5600`, insecure credentials. |
| MQTT diagnostics | `_dbgLogging = true`, enabling the Boost.MQTT5 debug logger. |

The gRPC TCP listener is independent of DGC's UDP video receiver on port 5600.
The server does not generate or relay camera video.

## Drone cache and persistence

Drone IDs are canonical lowercase UUID strings; new drones use UUID v4.
Client IDs are nonzero uint32 values. Only `CreateDevice` creates drones through
the server. MQTT metadata, attribute, and history updates require an existing
cached UUID and matching client ID. Unknown IDs are rejected.

The worker loads identities, current attributes, and history before the cache
constructor completes. Read failures abort startup. Approximately every 100 ms,
the worker synchronizes dirty snapshots and deletion tombstones, then discovers
SQL-only UUIDs. Newly seeded drones become available after a successful discovery
cycle; handlers do not perform fallback SQL queries themselves.

`CreateDevice` inserts a new cached drone with a `Device_<uuid>` default name.
`DeletDevice` immediately removes it from the cache and queues a tombstone. The
worker deletes the device's history, attributes, and identity transactionally;
clients and users remain. Pending tombstones prevent SQL discovery from
reimporting a drone whose deletion has not yet committed.

Nonempty metadata names containing only whitespace or any NUL byte are rejected
before modifying the cache. An empty name in an update means unchanged. The
server's `Drone` stores ID, client ID, name, online/state attributes, current
position, and simulator movement fields. It does not represent device type or
client name; existing SQL values for that metadata are preserved.

`DroneTimeSeries` retains timestamped position pairs and tracks its persisted
prefix. Replaying a timestamp updates its pair instead of appending a duplicate.
Correcting an older sample rewinds persistence progress so it is written again.
Historical updates do not move the current position backward in time.

Dirty snapshots are copied under the cache mutex; SQL work happens after that
mutex is released. Version counters preserve newer changes arriving during a
write. The worker's snapshot transaction stores identity when marked new,
current attributes, and new/corrected history. Ordinary updates cannot insert
unknown identities or change client ownership. Failed writes/deletions remain
pending for retry and are logged. Shutdown wakes and joins the worker.

RPC success acknowledges a cache change, not an SQL commit. Pending changes are
not crash-durable, and persistent database failures can leave them unsaved.
History currently has no retention or eviction limit.

## gRPC operations

The RPC definitions are in `DroneSDK/protocol/`:

| RPC | Request and behavior |
| --- | --- |
| `CreateDevice` | `Client`; allocate a cached UUID and default name, returning `Device`. |
| `DeletDevice` | `Device`; remove the cached drone and queue SQL deletion. The spelling is part of the current protocol. |
| `ListDevices` | `Client`; return that client's cached identities. |
| `UpdateAttribute` | `AttributeRequest`; update one attribute or a nonempty batch. |
| `GetAttribute` | `AttributeQuery`; select `online`, `state`, or `position` by name. |
| `GetAttributes` | `Device`; return current online, state, and position attributes. |
| `UpdatePosition` | `PositionRequest`; set coordinates with a server wall-clock timestamp. |
| `GetPosition` | `Device`; return current coordinates. |
| `AddTimeseries` | `TimeseriesRequest`; store complete latitude/longitude pairs. |
| `GetTimeseries` | `TimeseriesQuery`; select latitude or longitude at an optional exact timestamp, or the latest sample when omitted. |
| `GetTimeseriesRange` | `TimeseriesRangeQuery`; select inclusive optional time bounds and an optional series name. Empty name selects both coordinates. |

History is ordered by ascending timestamp, with latitude before longitude at
the same timestamp. Collection replies use `start=0` and `range=entry count`;
pagination is not implemented. Broad queries can exceed client receive limits.

Malformed UUIDs, unsupported names/types, and invalid values/bounds return
`INVALID_ARGUMENT`. Missing cached drones or exact samples return `NOT_FOUND`.
An empty history range returns a successful empty collection.

## MQTT telemetry

Publishers use per-drone topics. The server subscribes to `/device/+`,
`/device/+/attributes`, `/device/+/timeseries`, and `/status`:

| Topic | Protobuf payload | Supported data |
| --- | --- | --- |
| `/device/<uuid>` | `Device` | Existing device/client identity and nonempty name updates. |
| `/device/<uuid>/attributes` | `AttributeRequest` | `online` bool, `state` uint32, or `position`. |
| `/device/<uuid>/timeseries` | `TimeseriesRequest` | String-valued latitude/longitude samples sharing timestamps. |
| `/status` | Undefined | Subscribed, but unsupported by the handler. |

The topic structure selects the payload type. Drone and client identity still
come from the payload; the topic UUID is not used for cache lookup or compared
with the payload UUID. Unsupported topic structures are ignored.

Coordinates use decimal strings with round-trip double precision. Timestamp
strings are signed 64-bit UTC Unix milliseconds. A timeseries message must
contain both coordinates for every timestamp. Single attributes are supported;
a lone coordinate sample is rejected. Send either the single-value field or a
nonempty batch, not both. Validation completes before the cache entry changes.

Publishing and subscriptions use QoS 2, subject to broker approval. Publishing
also uses RETAIN. Each drone has a separate retained message for each topic
category. Retained timeseries contain only the latest published batch, not full
history, and may be replayed on subscription. Startup state comes from SQLite. QoS 2 is a transport guarantee, not an SQL commit
acknowledgment or a substitute for database uniqueness.

## Diagnostics and current limitations

- Configuration parsing and authentication are not implemented.
- MQTT disconnect is posted immediately before stopping the Asio context;
  protocol-level disconnect completion is not guaranteed.
- gRPC listener startup failure is logged, but `main` still returns zero.
- The simulator assumes successful discovery of at least 100 client-99 drones;
  its empty-result error path is not handled gracefully by `main`.
- Automated tests cover loopback gRPC and mocked MQTT delivery, not a live broker
  or camera stream.

Set `_dbgLogging=false` for less MQTT logging. For a manually managed broker,
`mosquitto -v` enables verbose output. Use broker logs and metrics to inspect
connections and subscriptions; they do not measure dirty cache entries or SQL
commit progress.
