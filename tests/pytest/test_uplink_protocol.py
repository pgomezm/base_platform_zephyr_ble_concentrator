"""Tests for the uplink decoder.

No hardware, no sockets. Every case here is bytes in and values out, which is
the whole reason uplink_protocol.py has no I/O in it.

What these are really guarding is the agreement between this decoder and
src/svc/comms/comms_protocol.hpp. That agreement has already been broken twice:
once when the record grew from 13 to 25 bytes, and once when a flags byte was
added to the header. Both times the C++ static_assert caught the firmware side.
This file is the other half.
"""

import base64
import struct

import pytest

from utils.uplink_protocol import (
    FLAG_BOOT,
    FLAG_HEARTBEAT,
    HEADER_SIZE,
    RECORD_SIZE,
    ProtocolError,
    parse_fragment,
    parse_header,
    take_fragment,
)

# A real fragment, captured from the LoRa gateway on the first end-to-end run:
# one endpoint, BOOT flag set, 37 bytes.
CAPTURED = base64.b64decode("AQAAAAEAAAEBAAABzCNcaTSUxQQAFDOeA+7/if8PBBUHAAAAAA==")


def make_header(record_count=0, sequence=1, flags=0, concentrator_id=1,
                fragment_index=0, fragment_count=1, dropped=0, evicted=0):
    return struct.pack("<IHBBBBBB", concentrator_id, sequence, fragment_index,
                       fragment_count, record_count, dropped, evicted, flags)


def make_record(address=b"\x01\x02\x03\x04\x05\x06", rssi=-60, seconds=3,
                temperature=21, humidity=50, pressure=1013,
                acc=(1, 2, 1000), battery=3000, timestamp=42):
    return struct.pack("<6sbHbBHhhhHI", address, rssi, seconds, temperature,
                       humidity, pressure, acc[0], acc[1], acc[2], battery, timestamp)


class TestSizes:
    """The sizes the firmware asserts on its side."""

    def test_header_is_twelve_bytes(self):
        assert HEADER_SIZE == 12

    def test_record_is_twenty_five_bytes(self):
        assert RECORD_SIZE == 25


class TestCapturedFragment:
    """Decode a frame that really came off a gateway."""

    def test_decodes_to_one_record(self):
        fragment = parse_fragment(CAPTURED)
        assert fragment.header.record_count == 1
        assert len(fragment.records) == 1
        assert fragment.size == 37

    def test_header_matches_the_capture(self):
        header = parse_fragment(CAPTURED).header
        assert header.concentrator_id == 1
        assert header.sequence == 1
        assert header.fragment_index == 0
        assert header.fragment_count == 1
        assert header.dropped_adv_reports == 0
        assert header.evicted_devices == 0
        assert header.is_boot
        assert not header.is_heartbeat

    def test_record_matches_the_capture(self):
        record = parse_fragment(CAPTURED).records[0]
        assert record.mac == "94:34:69:5C:23:CC"
        assert record.rssi == -59
        assert record.temperature == 20
        assert record.humidity == 51
        assert record.battery_mv == 1813
        # Z near 1000 mg is gravity: the endpoint was lying still.
        assert record.acc_z == 1039

    def test_mac_is_reversed_from_the_wire(self):
        record = parse_fragment(CAPTURED).records[0]
        assert record.mac_wire_order == "CC:23:5C:69:34:94"


class TestFlags:
    def test_heartbeat_has_no_records(self):
        fragment = parse_fragment(make_header(record_count=0, flags=FLAG_HEARTBEAT))
        assert fragment.header.is_heartbeat
        assert fragment.records == []

    def test_boot_and_heartbeat_can_both_be_set(self):
        header = parse_header(make_header(flags=FLAG_BOOT | FLAG_HEARTBEAT))
        assert header.is_boot
        assert header.is_heartbeat
        assert header.flag_names == ["BOOT", "HEARTBEAT"]

    def test_no_flags(self):
        assert parse_header(make_header(flags=0)).flag_names == []


class TestSignedFields:
    """The fields a wrong format string would silently mangle."""

    @pytest.mark.parametrize("rssi", [-128, -100, -1, 0, 127])
    def test_rssi_is_signed(self, rssi):
        data = make_header(record_count=1) + make_record(rssi=rssi)
        assert parse_fragment(data).records[0].rssi == rssi

    @pytest.mark.parametrize("temperature", [-40, -1, 0, 25, 127])
    def test_temperature_is_signed(self, temperature):
        data = make_header(record_count=1) + make_record(temperature=temperature)
        assert parse_fragment(data).records[0].temperature == temperature

    def test_accelerometer_axes_are_signed(self):
        data = make_header(record_count=1) + make_record(acc=(-32768, -1, 32767))
        record = parse_fragment(data).records[0]
        assert (record.acc_x, record.acc_y, record.acc_z) == (-32768, -1, 32767)

    def test_battery_is_unsigned(self):
        data = make_header(record_count=1) + make_record(battery=65535)
        assert parse_fragment(data).records[0].battery_mv == 65535


class TestStreaming:
    """take_fragment() over a byte stream that arrives in pieces."""

    def test_returns_nothing_until_the_header_is_complete(self):
        data = make_header(record_count=1) + make_record()
        fragment, rest = take_fragment(data[:5])
        assert fragment is None
        assert rest == data[:5]

    def test_returns_nothing_until_the_records_are_complete(self):
        data = make_header(record_count=1) + make_record()
        fragment, rest = take_fragment(data[:-1])
        assert fragment is None

    def test_takes_exactly_one_fragment_and_leaves_the_rest(self):
        first = make_header(record_count=1, sequence=1) + make_record()
        second = make_header(record_count=0, sequence=2)
        fragment, rest = take_fragment(first + second)
        assert fragment.header.sequence == 1
        assert rest == second

    def test_consecutive_fragments_drain_the_buffer(self):
        buffer = (make_header(record_count=0, sequence=1)
                  + make_header(record_count=1, sequence=2) + make_record()
                  + make_header(record_count=0, sequence=3))
        sequences = []
        while True:
            fragment, buffer = take_fragment(buffer)
            if fragment is None:
                break
            sequences.append(fragment.header.sequence)
        assert sequences == [1, 2, 3]
        assert buffer == b""


class TestDesync:
    """The stream carries no framing, so a lost byte is unrecoverable.

    These do not test a recovery path, because there is none. They test that the
    decoder says so instead of inventing a fragment out of noise.
    """

    def test_implausible_record_count_is_rejected(self):
        with pytest.raises(ProtocolError, match="lost sync"):
            parse_header(make_header(record_count=255))

    def test_short_buffer_is_rejected(self):
        with pytest.raises(ProtocolError):
            parse_header(b"\x00" * (HEADER_SIZE - 1))

    def test_trailing_bytes_are_rejected(self):
        data = make_header(record_count=0) + b"\xff"
        with pytest.raises(ProtocolError):
            parse_fragment(data)

    def test_missing_record_bytes_are_rejected(self):
        data = make_header(record_count=2) + make_record()
        with pytest.raises(ProtocolError):
            parse_fragment(data)
