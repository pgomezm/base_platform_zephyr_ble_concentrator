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

| inAir9 | DK header | nRF52840 pin |
| --- | --- | --- |
| MISO | D12 | P1.14 |
| MOSI | D11 | P1.13 |
| SCK | D13 | P1.15 |
| NSS | D10 | P1.12 |
| RESET | D9 | P1.11 |
| DIO0 | D2 | P1.03 |
| DIO1 | D3 | P1.04 |
| DIO2 | D4 | P1.05 |
| VCC | 3V3 | |
| GND | GND | |

MISO, MOSI and SCK are fixed by the board's SPI3 and are not a choice. The rest are set in the
overlay. These pin numbers were read back from the generated devicetree after a real build, not
taken from a datasheet.

`boards/nrf52840dk_nrf52840.overlay` explains why those indices are what they are. Read it before
wiring: the header index in devicetree is not the D number.

## Build

This has been built successfully against **Zephyr v4.4.1** with **Zephyr SDK 1.0.1**. Result:

```
FLASH:  120084 B / 1 MB    (11.45%)
RAM:     54704 B / 256 KB  (20.87%)
```

This repo is a **west manifest repo**: `west.yml` pins Zephyr v4.4.1 and declares `self: path: .`.
It therefore has to live *inside* a west workspace, not be one. Cloning it on its own and running
`cmake` in it will not work.

Two prerequisites are easy to get wrong and neither produces an obvious error message:

- **Python 3.12 or newer.** Zephyr 4.4 requires it. On 3.11 the build fails in CMake with
  "Could NOT find Python3 ... found unsuitable version", which reads like a missing package.
- **Zephyr SDK matching `zephyr/SDK_VERSION`** (1.0.1 at the pinned revision). The
  `arm-zephyr-eabi` toolchain alone is enough; the full SDK is a much larger download for no
  benefit here.

### Workspace layout

Keep the path short. Zephyr builds still hit Windows' `MAX_PATH` limit, and a deep workspace root
produces link errors that name a file rather than the real cause.

```
D:\zws\
├── .west\           created by west init
├── app_project\     this repo
├── zephyr\          fetched by west update, ~1.5 GB
└── modules\         fetched by west update
```

### One-time setup (Windows)

Host tools, once, from an elevated PowerShell:

```powershell
winget install Kitware.CMake Ninja-build.Ninja oss-winget.gperf oss-winget.dtc Git.Git Python.Python.3.12 7zip.7zip
```

Workspace and west:

```powershell
mkdir D:\zws
cd D:\zws
py -3.12 -m venv .venv
.venv\Scripts\activate
pip install west

git clone https://github.com/pgomezm/base_platform_zephyr_ble_concentrator.git app_project
west init -l app_project
west update                       # fetches Zephyr and its modules, slow the first time
west zephyr-export
pip install -r zephyr\scripts\requirements.txt
```

`west update` pulls in Zephyr's own module list, which is where `loramac-node` — the stack behind
`CONFIG_LORAWAN` — comes from.

Then the toolchain, whose version must match `zephyr\SDK_VERSION`:

```powershell
type zephyr\SDK_VERSION
west sdk install -t arm-zephyr-eabi
```

### One-time setup (Linux)

Same shape, and this is the combination the numbers above were measured on:

```sh
mkdir zws && cd zws
python3 -m venv .venv && source .venv/bin/activate
pip install west
git clone https://github.com/pgomezm/base_platform_zephyr_ble_concentrator.git app_project
west init -l app_project
west update && west zephyr-export
pip install -r zephyr/scripts/requirements.txt
west sdk install -t arm-zephyr-eabi
```

Also needed on the host: `cmake >= 3.20`, `ninja`, `gperf`, `dtc` (device-tree-compiler).

### Build and flash

From the workspace root, with the venv active:

```
west build -b nrf52840dk/nrf52840 app_project --pristine
west flash
```

The image lands in `build/zephyr/zephyr.hex`.

Note the board name is `nrf52840dk/nrf52840`, with a slash. The older `nrf52840dk_nrf52840` form
was replaced by hardware model v2 and no longer resolves.

Keep `--pristine` while the devicetree is still changing; an incremental build does not always pick
up an overlay edit.

`west flash` drives the DK's onboard J-Link, so it needs either the nRF Command Line Tools or the
J-Link software installed and on `PATH`.

### Day to day

```
west build                                                 # incremental
west build -t menuconfig                                   # inspect the resulting Kconfig
west build -b nrf52840dk/nrf52840 app_project -p always     # force clean
```

### A note on hal/os/freertos

`src/hal/os/freertos/os_freertos.cpp` is deliberately absent from `CMakeLists.txt` and is not
compiled by any of the commands above. It is a ready second backend for the `hal::os` seam, not
part of this firmware; see `src/hal/os/os.md`.

## Watching it run

The DK enumerates a USB CDC console at 115200 8N1. On Linux it is usually `/dev/ttyACM0`; on
Windows, check Device Manager for the COM port.

```sh
minicom -D /dev/ttyACM0 -b 115200
```

On Windows, PuTTY or Tera Term on the COM port at the same settings; the DK exposes three COM
ports and the console is the lowest-numbered of them.

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
