# Database schema

Create a new SQLite database from the DGCServer directory:

```sh
sqlite3 -bail dgc.sqlite < sql/create_schema.sql
```

Run the script once against an empty database. It intentionally fails if tables
already exist. Schema changes do not update existing databases automatically;
those need a migration. The server does not run this script at startup.

Relationships:

- A client has many users and devices.
- A device has many attributes and timeseries samples.
- All child records must reference an existing parent. Parent deletion is
  restricted until its children are explicitly removed.

Enable `PRAGMA foreign_keys = ON;` on **every** server database connection,
before starting a transaction. The setting in the script only applies to the
connection that runs the script.

Usernames are unique within each client and are case-sensitive. The user table
stores identity only; authentication credentials are not part of this schema.

Attributes contain one current value per device/name. Attribute and timeseries
values follow the proto types: boolean, string, or unsigned 32-bit integer;
attributes also support a latitude/longitude position. Exactly one value must
be set. The server converts position-attribute coordinate strings to numeric
degrees. Timeseries timestamps are stored as signed 64-bit UTC Unix milliseconds,
parsed from the proto timestamp string. Simulator latitude/longitude timeseries
values remain strings; they are distinct from position attributes.

Timeseries permits multiple samples with identical timestamps. Indexes support
device-wide time ranges and time ranges for a particular metric name.

## Constraints and indexes

Client and device IDs are restricted to `1..4294967295` to fit protocol `uint32`
IDs. Boolean values are 0 or 1, unsigned integer values are `0..4294967295`, and
string value columns reject BLOBs. Positions require both coordinates within
latitude `-90..90` and longitude `-180..180` degrees.

Unique constraints index users by client/username and attributes by device/name.
Additional indexes cover devices by client and timeseries by device/time or
by device/name/time. Parent deletion does not cascade.

The server connection enables foreign keys and stores MQTT messages using bound
parameters and one transaction per message, including all entries of a batch. Clients/devices are automatically
created from identity fields, even when telemetry arrives before registration.
Attributes are upserted; timeseries samples are appended. Invalid messages roll
back their identity and value writes together. A failure in any batch entry
rolls back earlier entries from the same batch. Legacy single-value requests
remain supported. There are no seed users, clients,
or devices. Application read queries and gRPC persistence are not implemented.

## MQTT writes and delivery semantics

No prior device registration is required. A message must contain nonzero device
and client IDs. Missing names are filled with `Device_<id>` / `Client_<id>`;
nonempty metadata updates existing rows. Device IDs are global, not scoped to a
client. Updating a device can change its client association.

Attribute writes replace all value columns for the same device/name, including
clearing columns from the previous value type. Timeseries writes always append;
repeated timestamps and repeated values are allowed. QoS 2 delivery does not
make database writes idempotent: retained-message replay or application resends
can append more samples. MQTT acknowledgments are not database commit receipts.

Run the integration test from DGCServer after building with `BUILD_TESTING=ON`:

```sh
ctest --test-dir build/local --output-on-failure
```

The test creates a temporary database from this schema. It checks simulator
payloads, automatic identity creation, typed values, attribute replacement,
history insertion, invalid-message rejection, rollback, and foreign keys,
without requiring an MQTT broker.
