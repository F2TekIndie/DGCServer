# Database schema

Create a new SQLite database from DGCServer:

```sh
sqlite3 -bail dgc.sqlite < sql/create_schema.sql
```

Run this once against an empty database. The server opens the database but does
not create tables automatically. `create_schema.sql` is the only setup script
and includes all columns, constraints, and indexes required by the current code.

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

MQTT and gRPC operations read or update `DroneCache` only. Its worker performs
all SQL access: startup hydration, periodic discovery of SQL-only UUIDs, snapshot
writes, and deletion processing. No handler synchronously queries SQL. External
creations become visible after the next successful discovery cycle.

Only `CreateDevice` creates new cached drones. It generates a UUID v4 and queues
a new snapshot; ordinary updates require an existing UUID and matching client.
`StoreDrone(Drone const&, DroneTimeSeries&, bool create)` inserts identity only
for new snapshots. Identity, attributes and history commit atomically. Client
name and device type are preserved for existing rows. Invalid UUIDs and empty,
whitespace-only or NUL-containing stored names are rejected. Empty names in
update envelopes mean unchanged and are permitted.

Deletion removes the cache entry immediately and queues a tombstone. The worker
deletes history, attributes and identity transactionally. Failed writes and
deletions remain pending for retry; tombstones prevent rehydration. Shutdown
asks the worker for a final flush. A successful RPC acknowledges cached state,
not a durable SQL commit.

To create drones directly in SQL, run `python3 scripts/create_drones.py X --db
dgc.sqlite`; optional `--client-id` defaults to 99. This is a standalone utility,
not a server request path.

## Position representation and uniqueness

`Drone` holds one current position; `DroneTimeSeries._position` retains history after successful writes. Its timestamp type is
`std::chrono::sys_time<std::chrono::milliseconds>`, stored as signed 64-bit UTC
Unix milliseconds. `DroneTimeSeries._persistedPositions` tracks the stored prefix; corrections
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

Client IDs must be `1..4294967295`. Device IDs and dependent foreign keys are
canonical lowercase non-nil UUID strings. Boolean values are 0 or 1; uint32
values are `0..4294967295`. String columns reject BLOBs. Each attribute/sample
has exactly one typed value. Latitude must be `-90..90` and longitude `-180..180`.

Unique constraints index user client/username and attribute device/name pairs.
Additional indexes support devices by client and timeseries by device/time or
device/name/time, plus the coordinate uniqueness index described above.

## Verification

After building with `BUILD_TESTING=ON`, run from DGCServer:

```sh
ctest --test-dir build/local --output-on-failure
```

The test uses a temporary database and the real cache worker, replacing only the
MQTT network boundary. It checks simulator batches, routing, rejection of unknown drones,
coordinate pairing/replay, snapshot persistence, retained history, timestamps,
validation, rollback, and foreign keys. No broker is required.

## Current-position timestamp

`device.position_timestamp_ms` stores the current position's UTC Unix-millisecond
timestamp. Its coordinates remain in the position attribute. Startup restores
this snapshot independently of the retained `DroneTimeSeries` history.
Historical corrections do not move the current snapshot backward in time.
