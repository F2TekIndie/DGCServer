-- Apply once to existing databases, with the server stopped.
-- Keep the most recently inserted value for each duplicate coordinate key.
BEGIN IMMEDIATE;
DELETE FROM timeseries
WHERE name IN ('latitude', 'longitude')
  AND id NOT IN (
      SELECT MAX(id) FROM timeseries
      WHERE name IN ('latitude', 'longitude')
      GROUP BY device_id, name, timestamp_ms
  );
CREATE UNIQUE INDEX IF NOT EXISTS idx_timeseries_position_unique
    ON timeseries(device_id, name, timestamp_ms)
    WHERE name IN ('latitude', 'longitude');
COMMIT;
