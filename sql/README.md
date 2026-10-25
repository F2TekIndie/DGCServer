# Database schema

Create a new SQLite database from the DGCServer directory:

```sh
sqlite3 -bail dgc.sqlite < sql/create_schema.sql
```

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
