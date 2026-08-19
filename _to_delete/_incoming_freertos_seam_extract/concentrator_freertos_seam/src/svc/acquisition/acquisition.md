# Acquisition Service

@defgroup grp_svc_acquisition Acquisition Service
@ingroup grp_svc
@brief BLE scan lifecycle and endpoint frame parsing

Obtains data from the sensors and passes it on, which is the same role the `acquisition` module has
in the 5K-IoT firmware architecture document. The difference here is only where the data comes
from: over the air from other devices, rather than from an ADC on this board.

## Contract

| | |
| --- | --- |
| **Owns** | The BLE scan lifecycle, the raw advertising report pool, and the Eddystone frame parser. The only writer into `svc::device_table`. |
| **Exposes** | `initialize()`, `get_port()`, `get_dropped_report_count()`. Nothing else calls into this service. |
| **Depends on** | `hal/ble`, `svc/device_table`. |

## The receive path, in order

1. **Zephyr's Bluetooth RX thread** runs `on_adv_report()`. It copies the report into the pool and
   posts one event. That is all it does.
2. **The acquisition thread** handles the event and drains the pool: parses each payload, discards
   anything that is not a custom frame with our company id, and writes what is left into
   `device_table`.

Splitting it this way is the whole point. Parsing in the callback would put a `memcpy`, a struct
copy and a table write inside the BLE stack's own thread, and the cost of falling behind there is
missed advertisements.

## Constraints

**The payload is copied before it is read as a struct.** The advertising buffer carries no
alignment guarantee, and the frame contains a `uint32_t`. Casting a pointer into that buffer would
be an unaligned read.

**A full pool drops the newest report and counts it.** It does not block the BLE thread, and it does
not silently overwrite. The count goes out in the uplink, so a room busier than the pool depth
reads as exactly that.

## Events

| Event | Meaning |
| --- | --- |
| `START_SCAN` | Begin passive scanning. |
| `STOP_SCAN` | Stop scanning. |
| `ADV_REPORT_AVAILABLE` | At least one report is in the pool. The handler drains all of them, so one event may cover several reports. |
