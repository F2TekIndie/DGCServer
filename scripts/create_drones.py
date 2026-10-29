#!/usr/bin/env python3
"""Create registered drones in an existing DGC SQLite database using SQL."""

import argparse
from pathlib import Path
import sqlite3
import sys
import uuid


def positive_int(text):
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("count", type=positive_int, help="number of drones to create")
    parser.add_argument("--db", type=Path, default=Path("dgc.sqlite"), help="existing database (default: dgc.sqlite)")
    parser.add_argument("--client-id", type=positive_int, default=99, help="owning client (default: 99, matching the simulator)")
    args = parser.parse_args()
    if args.client_id > 4294967295:
        parser.error("client ID must fit in uint32")

    try:
        # Open an existing database only; initialize its schema before running.
        connection = sqlite3.connect(args.db.resolve().as_uri() + "?mode=rw", uri=True)
        try:
            connection.execute("PRAGMA foreign_keys = ON")
            with connection:
                # Register all UUIDs in one transaction.
                connection.execute("BEGIN IMMEDIATE")
                connection.execute(
                    "INSERT INTO client(id, name) VALUES(?, ?) ON CONFLICT(id) DO NOTHING",
                    (args.client_id, f"Client_{args.client_id}"),
                )
                connection.executemany(
                    "INSERT INTO device(id, client_id, name) VALUES(?, ?, ?)",
                    ((str(device_id), args.client_id, f"Device_{device_id}") for device_id in (uuid.uuid4() for _ in range(args.count))),
                )
        finally:
            connection.close()
    except (sqlite3.Error, ValueError) as error:
        print(f"Could not create drones: {error}", file=sys.stderr)
        return 1
    print(f"Created {args.count} drones with UUIDs for client {args.client_id}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
