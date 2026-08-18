# base_platform_zephyr_ble_concentrator

BLE to LoRa concentrator firmware. It listens for `base_platform_baremetal_ble` sensor endpoints
advertising in a room, keeps the last reading from each of them, and every 15 minutes sends
everything it collected over LoRaWAN.

Runs on a Nordic nRF52840 DK with a Modtronix inAir9 (SX1276) radio on the Arduino header.

`docs/ARCHITECTURE.md` is the design document. This README covers building and running.

## Structure

The same layering as `deepsight-polaris-software`: an interface header per module, the
platform-specific implementation in a subdirectory under it, and one `.md` per module describing
what it owns and what it is not allowed to do.

```
src/
├── main.cpp                    entry point, calls app::initialize() and nothing else
├── config.hpp                  every Kconfig value, in one place
├── app/                        high-level logic and the device state machine
│   ├── app.cpp/.hpp
│   ├── port.cpp/.hpp           application events
│   ├── port_list.hpp           every port in the firmware
│   ├── tasks_priorities.hpp    thread priorities, and why they are in that order
│   └── state_machine/
│       ├── startup/            services up, waiting for the network
│       ├── listening/          scanning and collecting; the idle state
│       ├── dispatching/        an uplink is in flight; the busy state
│       ├── soft_error/         recoverable, retries up to a limit
│       └── hard_error/         unrecoverable, stops radiating
├── eda/                        event driven architecture
│   ├── active_object/          one thread plus one static event queue
│   ├── port/                   how a module is addressed
│   ├── state_machine/          flat state machine
│   └── timer/                  posts an event on expiry
├── hal/                        hardware abstraction
│   ├── ble/     ble.hpp     + zephyr/ble_zephyr.cpp
│   ├── lora/    lora.hpp    + zephyr/lora_zephyr.cpp
│   ├── system/  system.hpp  + zephyr/system_zephyr.cpp
│   ├── led/     led.hpp     + zephyr/led_zephyr.cpp
│   └── watchdog/watchdog.hpp+ zephyr/watchdog_zephyr.cpp
├── svc/                        services
│   ├── acquisition/            scan lifecycle and Eddystone parsing
│   ├── device_table/           last reading per device, keyed by BLE address
│   ├── comms/                  dispatch, fragmentation, the uplink wire format
│   └── system_diagnostics/     heartbeat, watchdog, health counters
└── utils/                      queue, logging
```

## Two rules worth knowing before reading the code

**Nothing of consequence runs in a Bluetooth callback.** The BLE callback copies the advertising
report into a static pool and posts one event. Parsing happens later, in the acquisition thread.
Anything that delays Zephyr's Bluetooth thread costs advertising reports.

**`hal::lora::send()` has exactly one caller, `svc::comms`.** Two contexts writing to the same SPI
radio corrupt each other quietly rather than failing loudly, so this is the constraint most worth
respecting. `svc::system_diagnostics` reads radio status and never transmits.

Both are explained in `docs/ARCHITECTURE.md` section 4, and each module's `.md` restates the part
that applies to it.

## Hardware

Modtronix inAir9, the plain one with the RFO output. The inAir9B is the +20 dBm variant and needs
`power-amplifier-output = "pa-boost"` in the overlay instead: setting that wrong still builds, it
just transmits into the wrong output stage.

| inAir9 | DK Arduino header |
| --- | --- |
| MISO / MOSI / SCK | D12 / D11 / D13, fixed by the board's SPI3 |
| NSS | D10 |
| RESET | D9 |
| DIO0 / DIO1 / DIO2 | D2 / D3 / D4 |
| VCC | 3V3 |
| GND | GND |

`boards/nrf52840dk_nrf52840.overlay` explains why those indices are what they are. Read it before
wiring: the header index in devicetree is not the D number.

## Build

One-time setup, if you do not already have a Zephyr environment:

```sh
python3 -m venv .venv
source .venv/bin/activate          # .venv\Scripts\activate on Windows
pip install west
```

Then, from an empty directory:

```sh
git clone <this-repo-url> base_platform_zephyr_ble_concentrator
cd base_platform_zephyr_ble_concentrator
west init -l .
west update                        # fetches Zephyr, slow the first time
west zephyr-export
pip install -r ../zephyr/scripts/requirements.txt
```

Build and flash:

```sh
west build -b nrf52840dk_nrf52840 . --pristine
west flash
```

Keep `--pristine` while the devicetree is still changing; an incremental build does not always pick
up an overlay edit.

## Watching it run

The DK enumerates a USB CDC console at 115200 8N1. On Linux it is usually `/dev/ttyACM0`; on
Windows, check Device Manager for the COM port.

```sh
minicom -D /dev/ttyACM0 -b 115200
```

Expect the state machine to log its transitions, `svc_acquisition` to report its pool depth, and
`svc_system_diagnostics` to print a health line once a minute with the number of devices tracked,
reports dropped and devices evicted.

## What does not work yet

**The LoRaWAN join is not implemented.** `hal::lora::join()` logs a warning and returns
`JOIN_ERROR`, because whether this device uses OTAA or ABP, and against which network server, has
not been decided. It fails deliberately rather than pretending to succeed, so the state machine
takes its error path instead of the firmware believing it is connected.

The consequence when you flash this today: it comes up, scans, collects readings into the device
table, and then cycles through `STARTUP` to `SOFT_ERROR` because it cannot join. The BLE half is
fully exercised; the LoRa half stops at the join.

Filling this in is a contained change in `hal/lora/zephyr/lora_zephyr.cpp` plus the credentials.
Nothing above that layer is affected.

Open items are listed in `docs/ARCHITECTURE.md` section 9.
