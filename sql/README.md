# SQLite schema

Storage belongs to DGCServer. The desktop application has no SQL access; it
communicates with the server through gRPC.

## Initialize and seed

Run from DGCServer against a new, empty database:

```sh
sqlite3 -bail dgc.sqlite < sql/create_schema.sql
python3 scripts/create_drones.py 100 --db dgc.sqlite
```

The server opens its configured file but does not create tables.
`create_schema.sql` includes all required tables, columns, constraints, and
indexes.

The seeding script opens an existing database, generates UUID v4 identities and
`Device_<uuid>` names, and inserts them in one transaction. `--client-id` defaults
to 99, matching the simulator; it creates that client if needed. It does not
seed telemetry history or users. If the server is already running, its worker
imports new identities on a later discovery cycle.

## Tables and identity

| Table | Purpose |
| --- | --- |
| `client` | Numeric client identity and name; parent for users and devices. |
| `user` | Client-linked user identity; username unique within a client. |
| `device` | UUID identity, client reference, name, type, and current-position timestamp. |
| `attributes` | One typed current value per device/name pair. |
| `timeseries` | Timestamped typed samples linked to a device. |

`device.id` is canonical lowercase non-nil UUID text. `attributes.device_id` and
`timeseries.device_id` are text foreign keys to it. Client IDs remain
`1..4294967295`; device type and user display name default to empty strings.

All child records require existing parents when foreign keys are enabled.
Deletion is restricted; nothing cascades. `database::DeleteDrone` explicitly
deletes history and attributes before deleting the identity. It retains the
client and any users.

Enable `PRAGMA foreign_keys=ON` on every connection before transactions. The
server and seeding script already do this. The schema script's setting applies
only to the connection running that script.

## Cache synchronization

Only the server's DB worker calls database read/write methods. MQTT and gRPC
handlers operate on `DroneCache`; there are no synchronous SQL fallbacks in
those request paths.

At startup, the worker loads a consistent snapshot of identities, current
attributes, and complete coordinate history. Restored history is marked
persisted. Read failures or malformed stored coordinate pairs abort startup.
Subsequent discovery scans identities and loads history only for UUIDs absent
from the cache and not pending deletion.

`StoreDrone(Drone const&, DroneTimeSeries&, bool create)` writes one transaction:

- Insert the client/device only when the snapshot is marked new.
- Update the existing device name and current-position timestamp.
- Upsert current `online`, `state`, and `position` attributes.
- Upsert new or corrected latitude/longitude history.

Ordinary updates cannot create a device or change its client association. The
server's C++ `Drone` does not represent client name or device type, so existing
SQL metadata for those fields is preserved. Application validation rejects
invalid UUIDs and blank or NUL-containing stored names. An empty name supplied
in an update means unchanged.

Failed transactions roll back and do not advance history's persisted prefix.
The cache keeps failed writes and deletions pending for worker retry. A deletion
tombstone keeps a pending SQL row out of the cache. A successful RPC means cache
acceptance; pending changes are not durable until SQL commits.

## Position and history representation

Current position is stored as numeric latitude/longitude in the `position`
attribute. `device.position_timestamp_ms` holds its signed 64-bit UTC Unix
millisecond timestamp. Startup restores this current snapshot independently
from history.

Historical coordinates are two string-valued rows, `latitude` and `longitude`,
with the same device UUID and timestamp. Cache ingestion requires both in one
message; incomplete pairs are rejected, with no cross-message pending buffer.

The unique partial index on `(device_id,name,timestamp_ms)` for those two names
lets replayed or corrected pairs update existing rows. Both coordinates commit
atomically. Other series names are representable by the schema but are not
currently supported by the server's cache ingestion.

`DroneTimeSeries._position` retains history after successful writes.
`_persistedPositions` identifies its stored prefix; correcting an older entry
rewinds that progress. History corrections do not move current position backward
in time. There is no automatic history eviction.

## Constraints and indexes

Attributes support bool, string, uint32, and position values. Timeseries support
bool, string, and uint32 values. Each row must have exactly one matching value
representation. Bool is 0 or 1, uint32 is `0..4294967295`, latitude is `-90..90`,
and longitude is `-180..180`. Timestamp columns store UTC Unix milliseconds.

Unique constraints index user client/username and attribute device/name pairs.
Additional indexes cover device client IDs, history device/time and
device/name/time, and unique coordinate samples.

## Verification

From DGCServer after building with `BUILD_TESTING=ON`:

```sh
ctest --test-dir build/local --output-on-failure
```

`mqtt_persistence` uses a temporary database and the real cache worker while
replacing the MQTT network boundary. `grpc_cache` exercises RPCs and worker
behavior through a local listener and temporary database. They cover UUIDs,
validation, snapshot/history persistence, SQL-only identity discovery,
transaction rollback, deletion retry, and foreign keys. No MQTT broker is needed.
