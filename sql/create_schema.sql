-- SQLite schema. Enable foreign keys on every application connection too.
PRAGMA foreign_keys = ON;

BEGIN TRANSACTION;

CREATE TABLE client (
    id INTEGER PRIMARY KEY CHECK (id BETWEEN 1 AND 4294967295),
    name TEXT NOT NULL CHECK (length(trim(name)) > 0)
);

CREATE TABLE "user" (
    id INTEGER PRIMARY KEY,
    client_id INTEGER NOT NULL REFERENCES client(id) ON DELETE RESTRICT,
    username TEXT NOT NULL CHECK (length(trim(username)) > 0),
    display_name TEXT NOT NULL DEFAULT '',
    UNIQUE (client_id, username)
);

CREATE TABLE device (
    id INTEGER PRIMARY KEY CHECK (id BETWEEN 1 AND 4294967295),
    client_id INTEGER NOT NULL REFERENCES client(id) ON DELETE RESTRICT,
    name TEXT NOT NULL CHECK (length(trim(name)) > 0),
    type TEXT NOT NULL DEFAULT ''
);

-- One current value per named device attribute; columns mirror Attribute.oneof.
CREATE TABLE attributes (
    id INTEGER PRIMARY KEY,
    device_id INTEGER NOT NULL REFERENCES device(id) ON DELETE RESTRICT,
    name TEXT NOT NULL CHECK (length(trim(name)) > 0),
    value_type TEXT NOT NULL CHECK (value_type IN ('bool', 'string', 'uint32', 'position')),
    bool_value INTEGER CHECK (bool_value IS NULL OR typeof(bool_value) = 'integer' AND bool_value IN (0, 1)),
    string_value TEXT CHECK (string_value IS NULL OR typeof(string_value) = 'text'),
    uint32_value INTEGER CHECK (uint32_value IS NULL OR typeof(uint32_value) = 'integer' AND uint32_value BETWEEN 0 AND 4294967295),
    latitude REAL CHECK (latitude IS NULL OR typeof(latitude) IN ('integer', 'real') AND latitude BETWEEN -90 AND 90),
    longitude REAL CHECK (longitude IS NULL OR typeof(longitude) IN ('integer', 'real') AND longitude BETWEEN -180 AND 180),
    UNIQUE (device_id, name),
    CHECK (
        (value_type = 'bool' AND bool_value IS NOT NULL AND string_value IS NULL AND uint32_value IS NULL AND latitude IS NULL AND longitude IS NULL)
        OR (value_type = 'string' AND bool_value IS NULL AND string_value IS NOT NULL AND uint32_value IS NULL AND latitude IS NULL AND longitude IS NULL)
        OR (value_type = 'uint32' AND bool_value IS NULL AND string_value IS NULL AND uint32_value IS NOT NULL AND latitude IS NULL AND longitude IS NULL)
        OR (value_type = 'position' AND bool_value IS NULL AND string_value IS NULL AND uint32_value IS NULL AND latitude IS NOT NULL AND longitude IS NOT NULL)
    )
);

-- Historical samples; timestamp_ms is UTC Unix time in milliseconds.
-- A position pair is unique per device/timestamp; other metrics may repeat.
CREATE TABLE timeseries (
    id INTEGER PRIMARY KEY,
    device_id INTEGER NOT NULL REFERENCES device(id) ON DELETE RESTRICT,
    name TEXT NOT NULL CHECK (length(trim(name)) > 0),
    timestamp_ms INTEGER NOT NULL CHECK (typeof(timestamp_ms) = 'integer'),
    value_type TEXT NOT NULL CHECK (value_type IN ('bool', 'string', 'uint32')),
    bool_value INTEGER CHECK (bool_value IS NULL OR typeof(bool_value) = 'integer' AND bool_value IN (0, 1)),
    string_value TEXT CHECK (string_value IS NULL OR typeof(string_value) = 'text'),
    uint32_value INTEGER CHECK (uint32_value IS NULL OR typeof(uint32_value) = 'integer' AND uint32_value BETWEEN 0 AND 4294967295),
    CHECK (
        (value_type = 'bool' AND bool_value IS NOT NULL AND string_value IS NULL AND uint32_value IS NULL)
        OR (value_type = 'string' AND bool_value IS NULL AND string_value IS NOT NULL AND uint32_value IS NULL)
        OR (value_type = 'uint32' AND bool_value IS NULL AND string_value IS NULL AND uint32_value IS NOT NULL)
    )
);

-- UNIQUE constraints already index user(client_id) and attributes(device_id).
CREATE INDEX idx_device_client ON device(client_id);
CREATE INDEX idx_timeseries_device_time ON timeseries(device_id, timestamp_ms);
CREATE INDEX idx_timeseries_device_name_time ON timeseries(device_id, name, timestamp_ms);

-- Coordinate rows together represent one position pair per device/timestamp.
CREATE UNIQUE INDEX idx_timeseries_position_unique ON timeseries(device_id, name, timestamp_ms)
    WHERE name IN ('latitude', 'longitude');

COMMIT;
