#!/usr/bin/env python3
"""Bench receiver for the concentrator's TCP and Wi-Fi variants.

Listens for the concentrator to connect, decodes each uplink and prints it. The
decoding lives in ``uplink_protocol.py`` next to this file, so the same code is
exercised by ``tests/pytest`` with no hardware attached.

    python tests/utils/uplink_server.py                     # decoded values
    python tests/utils/uplink_server.py --format raw        # hex, nothing interpreted
    python tests/utils/uplink_server.py --format both       # hex and values
    python tests/utils/uplink_server.py --port 5000 --csv run.csv

The concentrator is the client and this is the server, which is the way round
``hal::link`` works: the device opens the connection outward, so no inbound port
has to be opened on the device's side of the network.
"""

from __future__ import annotations

import argparse
import csv
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from uplink_protocol import (  # noqa: E402
    HEADER_SIZE,
    RECORD_SIZE,
    Fragment,
    ProtocolError,
    take_fragment,
)

CSV_COLUMNS = [
    "wall_time", "sequence", "fragment", "flags", "mac", "rssi",
    "seconds_since_seen", "temperature", "humidity", "pressure",
    "acc_x", "acc_y", "acc_z", "battery_mv", "endpoint_timestamp",
]


def render(fragment: Fragment, fmt: str) -> str:
    """Turn one fragment into what goes on the terminal."""
    h = fragment.header
    lines = []

    flags = ",".join(h.flag_names) or "-"
    lines.append(
        f"[{time.strftime('%H:%M:%S')}] concentrator 0x{h.concentrator_id:08X}  "
        f"seq {h.sequence}  fragment {h.fragment_index + 1}/{h.fragment_count}  "
        f"{h.record_count} record(s)  {fragment.size} B  flags {flags}"
    )

    # Counters are on their own line and only when non-zero, because a zero
    # every cycle trains you to stop reading the line the day it is not zero.
    if h.dropped_adv_reports or h.evicted_devices:
        lines.append(
            f"           ! dropped_adv_reports {h.dropped_adv_reports}  "
            f"evicted_devices {h.evicted_devices}"
        )

    if fmt in ("raw", "both"):
        head = fragment.raw[:HEADER_SIZE].hex(" ")
        lines.append(f"           header  {head}")
        for i in range(h.record_count):
            start = HEADER_SIZE + i * RECORD_SIZE
            body = fragment.raw[start : start + RECORD_SIZE].hex(" ")
            lines.append(f"           record  {body}")

    if fmt in ("values", "both"):
        if not fragment.records:
            lines.append("           (no records - heartbeat)")
        for r in fragment.records:
            lines.append(
                f"           {r.mac}  rssi {r.rssi:>4} dBm  seen {r.seconds_since_seen:>5}s ago  "
                f"{r.temperature:>4} C  {r.humidity:>3} %  p {r.pressure:>6}  "
                f"acc {r.acc_x:>6}/{r.acc_y:>6}/{r.acc_z:>6}  "
                f"batt {r.battery_mv:>5} mV  ts {r.endpoint_timestamp}"
            )

    return "\n".join(lines)


def write_csv_rows(writer, fragment: Fragment) -> None:
    h = fragment.header
    stamp = time.strftime("%Y-%m-%dT%H:%M:%S")
    flags = "|".join(h.flag_names)
    for r in fragment.records:
        writer.writerow({
            "wall_time": stamp,
            "sequence": h.sequence,
            "fragment": f"{h.fragment_index + 1}/{h.fragment_count}",
            "flags": flags,
            "mac": r.mac,
            "rssi": r.rssi,
            "seconds_since_seen": r.seconds_since_seen,
            "temperature": r.temperature,
            "humidity": r.humidity,
            "pressure": r.pressure,
            "acc_x": r.acc_x,
            "acc_y": r.acc_y,
            "acc_z": r.acc_z,
            "battery_mv": r.battery_mv,
            "endpoint_timestamp": r.endpoint_timestamp,
        })


def serve_one(conn: socket.socket, peer, fmt: str, writer) -> None:
    """Read fragments off one connection until the peer closes or desyncs."""
    print(f"-- connected: {peer[0]}:{peer[1]}")
    buffer = b""
    fragments = 0

    while True:
        chunk = conn.recv(4096)
        if not chunk:
            print(f"-- closed by {peer[0]}:{peer[1]} after {fragments} fragment(s)")
            return

        buffer += chunk

        while True:
            try:
                fragment, buffer = take_fragment(buffer)
            except ProtocolError as error:
                # Unrecoverable: the stream carries no framing to resynchronise
                # on. Dropping the connection is honest - the concentrator will
                # reconnect and start a fresh, aligned stream.
                print(f"!! {error}")
                print("!! dropping the connection so the device reconnects cleanly")
                return

            if fragment is None:
                break

            fragments += 1
            print(render(fragment, fmt))
            if writer is not None:
                write_csv_rows(writer, fragment)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Receive and decode uplinks from the BLE concentrator.",
    )
    parser.add_argument("--host", default="0.0.0.0",
                        help="address to bind (default: every interface)")
    parser.add_argument("--port", type=int, default=5000,
                        help="TCP port to listen on (default: 5000)")
    parser.add_argument("--format", choices=("values", "raw", "both"), default="values",
                        help="values decodes the records, raw prints the bytes "
                             "and interprets nothing (default: values)")
    parser.add_argument("--csv", metavar="PATH",
                        help="also append every record to this CSV file")
    parser.add_argument("--once", action="store_true",
                        help="exit after the first connection closes")
    args = parser.parse_args()

    csv_file = None
    writer = None
    if args.csv:
        path = Path(args.csv)
        new = not path.exists()
        csv_file = path.open("a", newline="", encoding="utf-8")
        writer = csv.DictWriter(csv_file, fieldnames=CSV_COLUMNS)
        if new:
            writer.writeheader()
        print(f"-- appending records to {path}")

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    try:
        server.bind((args.host, args.port))
    except OSError as error:
        print(f"!! cannot bind {args.host}:{args.port}: {error}", file=sys.stderr)
        return 1

    server.listen(1)
    print(f"-- listening on {args.host}:{args.port}, format {args.format}")
    print("-- set CONFIG_APP_LINK_WIFI_SERVER_ADDR to this machine's address "
          "on the concentrator's network")

    try:
        while True:
            conn, peer = server.accept()
            with conn:
                try:
                    serve_one(conn, peer, args.format, writer)
                except ConnectionResetError:
                    print(f"-- reset by {peer[0]}:{peer[1]}")
            if csv_file is not None:
                csv_file.flush()
            if args.once:
                return 0
    except KeyboardInterrupt:
        print("\n-- stopped")
        return 0
    finally:
        server.close()
        if csv_file is not None:
            csv_file.close()


if __name__ == "__main__":
    raise SystemExit(main())
