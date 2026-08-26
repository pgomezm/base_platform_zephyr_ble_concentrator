# tests

Everything that talks to a running concentrator, and everything that checks the
firmware from outside it. Laid out the way `deepsight-polaris-software/tests`
is:

```
tests/
  utils/     reusable clients and decoders - importable, and runnable by hand
  pytest/    pytest.ini, conftest.py, test_*.py
```

`utils/` is not test code. It is the client side of the device's protocols,
which a person uses at the bench and a test imports. Polaris keeps
`scpi_client.py` and `scan_frame_receiver.py` there for the same reason.

## Receiving uplinks

`utils/uplink_server.py` is the other end of the TCP and Wi-Fi variants. The
concentrator connects outward to it — no inbound port has to be opened on the
device's side of the network — and sends fragments, which this decodes and
prints.

```sh
python tests/utils/uplink_server.py                     # decoded values
python tests/utils/uplink_server.py --format raw        # hex, nothing interpreted
python tests/utils/uplink_server.py --format both       # hex and values
python tests/utils/uplink_server.py --csv run.csv       # also append every record
python tests/utils/uplink_server.py --port 5000 --once  # exit after one connection
```

Point the device at whichever address this machine has on the concentrator's
network, in a gitignored `prj_local.conf`:

```
CONFIG_APP_LINK_WIFI_SERVER_ADDR="192.168.x.y"
CONFIG_APP_LINK_WIFI_SERVER_PORT=5000
```

`--format raw` exists because the two questions are different. Values answer
"what did the sensor read"; raw answers "did the right bytes arrive", which is
the one worth asking when a field looks wrong and it is not yet clear whether
the firmware, this decoder or the endpoint is lying.

### The decoder is separate from the receiver

`utils/uplink_protocol.py` has no sockets and no printing: bytes in, values out.
That is what lets the tests exercise it with no hardware, and it is why a
capture pasted from a gateway can be decoded without running anything.

It mirrors `src/svc/comms/comms_protocol.hpp`, and when one changes the other
must. Both sides carry a size check for exactly that reason — `static_assert` in
the C++, `HEADER_SIZE`/`RECORD_SIZE` and a test here.

### Known limitation: the stream has no framing

`SocketLink::send()` writes the fragment and nothing else — no magic number, no
length prefix, no version byte. What makes reading it possible at all is that
the header declares `record_count`, so the length is computable once 12 bytes
have arrived.

The consequence: a reader that starts mid-fragment, or a stream that loses a
byte, desynchronises permanently. There is nothing to resynchronise on. The
receiver detects it (an implausible `record_count`) and drops the connection so
the device reconnects onto a fresh, aligned stream.

That is fine at a bench. It is **not** fine for a production backend, and the
fix belongs in the wire format rather than in every reader: a magic number and a
length prefix ahead of the header. Not done yet, deliberately — the format is
still moving, and the LoRa variant has no framing problem to solve because
LoRaWAN delivers whole packets.

## Running the tests

```sh
python -m pytest tests/pytest -v
```

No hardware. The cases cover the struct sizes, a real fragment captured from the
gateway on the first end-to-end run, the signed fields that a wrong format
string would silently mangle, stream reassembly across split packets, and the
desync cases.

The reason they exist: the agreement between the firmware and this decoder has
already broken twice — once when the record grew from 13 to 25 bytes, once when
a flags byte was added to the header. Both times the `static_assert` caught the
firmware side. This is the other half.

`conftest.py` adds `--host` and `--port` for tests that need a live device.
Nothing uses them yet; they are there so a hardware test has somewhere to read
its bench parameters from, the same way polaris does it.
