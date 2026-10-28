# Database schema

Create a new SQLite database from DGCServer:

```sh
sqlite3 -bail dgc.sqlite < sql/create_schema.sql
```

Run this once against an empty database. The server opens the database but does
not create or migrate tables. Existing databases require the migration below.

## Tables and relationships

- `client`: parent for users and devices.
- `user`: client identity; usernames are case-sensitive and unique per client.
- `device`: client-linked device identity, with a globally unique device ID.
- `attributes`: one current value per device/name.
- `timeseries`: device-linked historical samples.

All child records require an existing parent. Deletion is restricted until
children are explicitly removed; nothing cascades. Authentication credentials
and seed records are not supplied.

Enable `PRAGMA foreign_keys = ON` on each database connection before starting a
transaction. The server wrapper already does this. The setting in the schema
script applies only to its own connection.

## Current persistence path

MQTT payloads update `DroneCache`; a worker synchronizes dirty drone snapshots
approximately every 100 ms through `StoreDrone(Drone&)`. No prior registration
is required. Missing clients/devices are created with `Client_<id>` /
`Device_<id>` names. Nonempty drone names update existing records. Client name
and device type are preserved on existing rows because `Drone` has no fields
for them. A device's client association can change.

One transaction stores identity, `online`, `state`, the newest available
`position` attribute, and new/corrected position history. Bound parameters keep
payloads separate from SQL. Failures roll back the snapshot and do not advance
its persistence progress. The cache retains data for another attempt.

The schema supports boolean, string, uint32, and position attributes. Current
cache ingestion supports only online/state/position attributes and latitude/
longitude timeseries samples. Broader application queries and gRPC database
operations remain unimplemented.

## Position representation and uniqueness

`Drone._position` retains history after successful writes. Its timestamp type is
`std::chrono::sys_time<std::chrono::milliseconds>`, stored as signed 64-bit UTC
Unix milliseconds. `_persistedPositions` tracks the stored prefix; corrections
to an older entry cause that portion to be synchronized again.

Drone ID plus timestamp uniquely identifies a latitude/longitude pair in the
cache. Both coordinates must arrive in the same timeseries message for that
timestamp. Incomplete messages are rejected; there is no pending-pair buffer.

SQLite stores the pair as two string-valued rows named `latitude` and
`longitude`, updated in one transaction. A unique partial index on
`(device_id, name, timestamp_ms)` for these names prevents duplicates; replayed
pairs update existing values. Other metric names are not covered by that index.
The current `position` attribute stores numeric degrees instead.

QoS 2 controls MQTT delivery, not database commits. Cached updates can be lost
on process failure before synchronization. Shutdown attempts a final flush.
History currently has no automatic eviction policy.

## Constraints and indexes

Client/device IDs must be `1..4294967295`. Boolean values are 0 or 1; uint32
values are `0..4294967295`. String columns reject BLOBs. Each attribute/sample
has exactly one typed value. Latitude must be `-90..90` and longitude `-180..180`.

Unique constraints index user client/username and attribute device/name pairs.
Additional indexes support devices by client and timeseries by device/time or
device/name/time, plus the coordinate uniqueness index described above.

## Existing databases: unique positions

Stop the server and back up the database, then run from DGCServer:

```sh
sqlite3 -bail dgc.sqlite < sql/update_001_unique_positions.sql
```

The migration retains the highest-ID row for each duplicate coordinate
device/name/timestamp key and adds the unique partial index. Fresh databases
already include this index. Schema scripts do not run automatically.

## Verification

After building with `BUILD_TESTING=ON`, run from DGCServer:

```sh
ctest --test-dir build/local --output-on-failure
```

The test uses a temporary database and the real cache worker, replacing only the
MQTT network boundary. It checks simulator batches, routing, unknown drones,
coordinate pairing/replay, snapshot persistence, retained history, timestamps,
validation, rollback, and foreign keys. No broker is required.
