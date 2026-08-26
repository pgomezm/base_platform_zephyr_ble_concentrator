"""Decoder for the concentrator's uplink wire format.

Pure functions over bytes: no sockets, no printing, no state. That is what lets
the same code serve the bench tool and the pytest suite, and it is why the tests
need no hardware.

The format is defined in ``src/svc/comms/comms_protocol.hpp`` and this file
mirrors it. **When one changes the other must.** Both sides carry a size
assertion for exactly that reason - the C++ has ``static_assert``, this has
``HEADER_SIZE``/``RECORD_SIZE`` and a test that checks them.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import List, Optional, Tuple

# --- wire format -------------------------------------------------------------
#
# Both structs are __attribute__((packed)) and every target is little-endian,
# so "<" with no alignment is an exact mirror.

_HEADER_FMT = "<IHBBBBBB"
_RECORD_FMT = "<6sbHbBHhhhHI"

HEADER_SIZE = struct.calcsize(_HEADER_FMT)   # 12
RECORD_SIZE = struct.calcsize(_RECORD_FMT)   # 25

FLAG_BOOT = 0x01
FLAG_HEARTBEAT = 0x02

#: A record count above this means the stream is desynchronised rather than
#: busy. The firmware's own table is capped at CONFIG_APP_MAX_DEVICES, and a
#: fragment only ever carries what fits in one packet.
MAX_PLAUSIBLE_RECORDS = 200


class ProtocolError(Exception):
    """The bytes do not describe a valid uplink."""


@dataclass
class Record:
    """One endpoint, exactly as the concentrator relayed it.

    Nothing here is interpreted. The concentrator is a relay - it does not
    decide what an accelerometer reading means, and neither does this class.
    Deriving motion, occupancy or health from these numbers is the backend's
    job; see docs/ARCHITECTURE.md section 4.2.
    """

    address: bytes
    rssi: int
    seconds_since_seen: int
    temperature: int
    humidity: int
    pressure: int
    acc_x: int
    acc_y: int
    acc_z: int
    battery_mv: int
    endpoint_timestamp: int

    @property
    def mac(self) -> str:
        """The address in the conventional most-significant-byte-first form.

        Zephyr hands out advertiser addresses least-significant byte first, and
        the firmware relays them untouched, so the bytes arrive reversed with
        respect to how a MAC is written down or printed by a phone scanner.
        """
        return ":".join(f"{b:02X}" for b in reversed(self.address))

    @property
    def mac_wire_order(self) -> str:
        """The address in the order it actually appears on the wire."""
        return ":".join(f"{b:02X}" for b in self.address)


@dataclass
class Header:
    """The 12 bytes at the front of every fragment."""

    concentrator_id: int
    sequence: int
    fragment_index: int
    fragment_count: int
    record_count: int
    dropped_adv_reports: int
    evicted_devices: int
    flags: int

    @property
    def is_boot(self) -> bool:
        """First uplink since the concentrator restarted."""
        return bool(self.flags & FLAG_BOOT)

    @property
    def is_heartbeat(self) -> bool:
        """No records: the device is alive and had nothing new to report."""
        return bool(self.flags & FLAG_HEARTBEAT)

    @property
    def flag_names(self) -> List[str]:
        names = []
        if self.is_boot:
            names.append("BOOT")
        if self.is_heartbeat:
            names.append("HEARTBEAT")
        return names


@dataclass
class Fragment:
    """One uplink fragment: a header and the records it carried."""

    header: Header
    records: List[Record]
    raw: bytes

    @property
    def size(self) -> int:
        return len(self.raw)


def parse_header(data: bytes) -> Header:
    """Decode the 12 byte header.

    :raises ProtocolError: if there are not enough bytes, or the record count is
        implausible - which over a stream with no framing means the reader has
        lost sync rather than that the device sent something odd.
    """
    if len(data) < HEADER_SIZE:
        raise ProtocolError(f"need {HEADER_SIZE} bytes for a header, got {len(data)}")

    fields = struct.unpack(_HEADER_FMT, data[:HEADER_SIZE])
    header = Header(*fields)

    if header.record_count > MAX_PLAUSIBLE_RECORDS:
        raise ProtocolError(
            f"record_count is {header.record_count}, which is not plausible; "
            "the stream has lost sync"
        )

    return header


def parse_fragment(data: bytes) -> Fragment:
    """Decode a complete fragment: header plus exactly its records.

    :raises ProtocolError: if the buffer is short or has trailing bytes.
    """
    header = parse_header(data)
    expected = HEADER_SIZE + header.record_count * RECORD_SIZE

    if len(data) != expected:
        raise ProtocolError(
            f"header declares {header.record_count} records, so the fragment is "
            f"{expected} bytes, but {len(data)} were given"
        )

    records = []
    for i in range(header.record_count):
        start = HEADER_SIZE + i * RECORD_SIZE
        addr, rssi, secs, temp, hum, pres, ax, ay, az, batt, ts = struct.unpack(
            _RECORD_FMT, data[start : start + RECORD_SIZE]
        )
        records.append(Record(addr, rssi, secs, temp, hum, pres, ax, ay, az, batt, ts))

    return Fragment(header=header, records=records, raw=data)


def take_fragment(buffer: bytes) -> Tuple[Optional[Fragment], bytes]:
    """Pull one complete fragment off the front of a stream buffer.

    Returns ``(fragment, rest)``, or ``(None, buffer)`` when more bytes are
    needed. Never blocks and never discards on a short read.

    **The stream carries no framing of its own.** ``SocketLink::send()`` writes
    the fragment and nothing else - no magic number, no length prefix, no
    version. What makes this work at all is that the header declares
    ``record_count``, so the length is computable once 12 bytes have arrived.

    The consequence is worth knowing before trusting it: a reader that starts
    mid-fragment, or a single lost byte, desynchronises permanently. There is
    nothing to resynchronise on. That is acceptable for a bench tool on a fresh
    connection, and it is not acceptable for a production backend - which wants
    a length-prefixed or delimited frame. See the note in tools/README.md.
    """
    if len(buffer) < HEADER_SIZE:
        return None, buffer

    header = parse_header(buffer)
    total = HEADER_SIZE + header.record_count * RECORD_SIZE

    if len(buffer) < total:
        return None, buffer

    return parse_fragment(buffer[:total]), buffer[total:]
