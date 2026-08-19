# BLE HAL

@defgroup grp_hal_ble BLE HAL
@ingroup grp_hal
@brief Hardware abstraction for the BLE radio

Abstracts the nRF52840's native BLE radio in Observer role.

## Contract

| | |
| --- | --- |
| **Owns** | The BLE radio: stack initialization, scan lifecycle, the raw advertising report callback. |
| **Exposes** | `initialize()`, `start_scan()`, `stop_scan()`, `is_scanning()`, and a registration point for the report callback. |
| **Depends on** | Zephyr's Bluetooth API only. |

## Constraints

**This module never parses payload content.** It copies the advertising bytes, the advertiser
address and the locally measured RSSI into an `AdvReport` and hands that up. Everything
Eddystone-specific lives in `svc/acquisition`. If the endpoint's frame format changes, this file
does not.

**The scan callback runs in Zephyr's Bluetooth RX thread.** It copies and returns. It does not
parse, block or allocate, because anything that delays that thread costs advertising reports.

## Role

Observer only. The concentrator never advertises, never accepts a connection, and is never a GATT
client or server. There is no `conn`-style registry here because there are no connections.
