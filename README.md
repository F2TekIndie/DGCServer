# DGCServer

**Status: proof-of-concept prototype.** The simulator publishes telemetry over
MQTT and the server persists it in SQLite. Desktop telemetry integration remains incomplete. This is not production-ready.

## Current behavior

| Executable | Current behavior |
| --- | --- |
| `dgcserver` | Opens SQLite, starts MQTT subscription processing and a gRPC server, then waits for Ctrl+C (SIGINT). |
| `dgcsim` | Simulates drones, publishes Protobuf identity/attributes/position samples over MQTT, and runs until Ctrl+C. |

The gRPC services read and write through the drone cache. MQTT device, attribute, and timeseries payloads
are deserialized into an in-memory drone cache and synchronized to SQLite by a
worker. Drones must be registered before telemetry updates are accepted. The database wrapper enables foreign
keys, but does not create tables.

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
MQTT publish per drone, rather than one publish per value. Every request
includes device/client identity. Coordinates
use string values with round-trip double precision, and timestamp strings are
UTC Unix milliseconds. Both coordinate samples share one timestamp. Initial
values are sent for every drone; later position samples are sent for traveling
drones. Server handlers validate these messages and update the cache; the worker
persists each dirty drone snapshot transactionally.

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
- `server/grpc.*`: cache-backed services, listener, and waiting worker.
- `server/mqsubscriptions.*`: topic routing and Protobuf decoding into the cache.
- `server/dronecache.*`: cache keyed by drone ID and background synchronization.
- `sdk/drone.h`: shared drone state, retained positions, and persistence progress.
- `sim/dronemanager.*`: randomized state/movement updates and Protobuf publishing.
- `sql/create_schema.sql`: complete new-database setup, including position
  uniqueness and current-position timestamps.

Shutdown currently posts MQTT disconnect and immediately stops its context, so
protocol-level disconnect completion is not guaranteed. Subscription handling
logs every result but decides subscription success from only the first result.
Exceptions escaping MQTT callbacks can terminate the worker/process. gRPC startup failure is reported, but the entry point still
returns zero. Configuration parsing, authentication,
and live-broker end-to-end tests remain future work.

## MQTT persistence

Initialize a new database with `sql/create_schema.sql` before starting the
server. This single script includes all required tables, columns, and indexes;
see the [database README](sql/README.md).

The cache worker hydrates stored drones, attributes, and history before the
server accepts requests. It then runs approximately every 100 ms, persisting
dirty snapshots, processing deletion tombstones, and discovering SQL-only drone
UUIDs. Only that worker calls database read/write methods. MQTT and gRPC requests
read or change memory only; SQL-created drones become available after the next
successful discovery cycle. Unknown UUIDs are rejected until then.

Drone IDs are random UUIDs represented as canonical lowercase strings in the
cache, SQLite, and Protobuf `Device.id`. Client IDs remain nonzero uint32 values.
Only `CreateDevice` creates drones through the server. Other writes require an
existing cached drone with the matching client. Nonempty names containing only
whitespace or any NUL byte are rejected before changing any state. An omitted
(empty) name preserves the current name.

The receive handler accepts metadata, `online` boolean and `state` uint32
attributes, and latitude/longitude history. The `position` attribute receives a
server wall-clock timestamp. Unsupported names and wrong value types are
rejected. Each batch is validated before modifying its entry. `/status` remains
unsupported.

Timeseries batches must contain both latitude and longitude for each timestamp
in the same message. There is no pending-coordinate storage across messages.
Legacy single attributes are supported, but a lone latitude or longitude sample
is rejected. Requests must use either the single-value or batch field, not both.

`Drone` holds current attributes and a single `_position`. `DroneTimeSeries`
holds retained position history separately. Positions use
`std::chrono::sys_time<std::chrono::milliseconds>` and remain in `DroneTimeSeries._position`
after storage. Drone ID plus timestamp uniquely identifies a position pair:
replays do not append duplicates, and changed coordinates update that pair.
`DroneTimeSeries._persistedPositions` tracks the stored prefix; correcting an older position
rewinds this progress so it is written again.

Every 100 ms, the worker copies dirty snapshots while holding the cache mutex,
then releases it before database I/O. Version counters keep newer updates dirty
when they arrive during a write. Failed writes leave history and progress intact
for retry. Cache destruction asks the worker for a final flush, then joins it. A
persistent database error is logged; the in-memory cache is not crash-durable.

`StoreDrone(Drone const&, DroneTimeSeries&, bool create)` is a worker persistence
operation. Only snapshots marked new can insert a client/device; updates never
create an unknown identity or change ownership. Identity, current attributes,
and new/corrected historical coordinates commit in one transaction. New names
default to `Device_<uuid>`. Device type and client name are preserved on existing
SQL records because `Drone` does not represent those fields.

Latitude/longitude history remains string-valued in SQLite, while the current
position attribute uses numeric degrees. Both coordinate rows are upserted
atomically, with a unique index on device/name/timestamp for those two metrics.
Database uniqueness prevents duplicate position rows across cache restarts;
MQTT QoS 2 itself is not a database commit acknowledgment.

History is currently retained without an eviction limit. Monitoring cache size,
flush latency, and dirty-drone count is not implemented yet. Rebuild simulator
and server together after protocol changes.

Register simulator drones in an initialized database before starting telemetry:

```sh
python3 scripts/create_drones.py 100 --db dgc.sqlite
```

The script uses SQL directly and defaults to client 99, matching the simulator.
Use `--client-id` to choose another client. It generates UUID v4 IDs and default
names in one transaction. An initialized database is required. Start DGCServer
before `dgcsim`; the simulator obtains the first requested number of registered
UUIDs for client 99 using `ListDevices`, rather than inventing numeric IDs.

Initialize development databases with the current `sql/create_schema.sql`.
Regenerate and rebuild Protobuf consumers after protocol changes.

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

SQLite writes run on the cache worker, outside the MQTT receive handler. There
is no dirty-cache counter or throughput/latency reporting yet. Broker
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

## gRPC cache operations

All RPC handlers interact only with `DroneCache`. They neither call SQL nor wait
for the worker. Lists are served from memory and never reload SQL history.

`CreateDevice` allocates a UUID v4 and `Device_<uuid>` name in memory. `DeletDevice`
immediately removes the cached entry and queues a deletion tombstone. The worker
later removes SQL history, attributes, and identity in one transaction; clients
and users remain. A tombstone prevents SQL refresh from reimporting a deleted
drone while deletion is pending or failing. In-flight writes are followed by
deletions on the same worker. Replacements get new UUIDs, so delayed telemetry
for a deleted identity cannot update the replacement.

`GetAttribute` now accepts `AttributeQuery` with `device` and `name` (`online`,
`state`, or `position`). `GetTimeseries` accepts `TimeseriesQuery` with `device`,
`name` (`latitude` or `longitude`), and optional `timestamp_ms`. Omit the timestamp
for the latest historical sample. `GetTimeseriesRange` accepts
`TimeseriesRangeQuery` with optional inclusive `start_timestamp_ms` and
`end_timestamp_ms`; omitted bounds are unbounded. An empty series name selects
both coordinates. Results are ordered by ascending timestamp, latitude before
longitude at the same timestamp. Collection responses use `start=0` and
`range=number of returned entries`; pagination is not implemented.

Regenerate and rebuild clients after protocol changes. Unknown drones/samples
return `NOT_FOUND`; malformed UUIDs, invalid names, selectors, bounds, and values
return `INVALID_ARGUMENT`. Successful writes, including creation/deletion, mean
the cache accepted the operation, not that SQLite committed it. SQL failures
remain pending for worker retry and are logged. A process crash or persistent
SQL error during shutdown can lose pending changes. Tests cover all RPC methods,
cache operations while SQL is exclusively locked, deletion retry/tombstones,
UUIDs, name validation, MQTT routing, persistence, and SQL drone creation.
