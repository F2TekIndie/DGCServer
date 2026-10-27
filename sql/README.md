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
be set. Positions are stored as numeric degrees, so the server must convert
the proto's coordinate strings. Timeseries timestamps are UTC Unix milliseconds;
the server must convert the proto's timestamp string to this representation.

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

The current server connection enables foreign keys, but does not yet implement
inserts, queries, or conversion from protocol values. There are no seed users,
clients, or devices in the initial schema.
