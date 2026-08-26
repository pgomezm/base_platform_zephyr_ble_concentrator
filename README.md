# base_platform_zephyr_ble_concentrator

BLE concentrator firmware. It listens for `base_platform_baremetal_ble` sensor endpoints
advertising in a room, keeps the last reading from each of them, and every 15 minutes sends
everything it collected upstream.

It ships in two variants that differ only in how those readings leave the device — LoRaWAN or TCP —
and both run on the same Nordic nRF52840 DK. See "The two variants" below.

`docs/ARCHITECTURE.md` is the design document. This README covers building and running.

## Structure

The same layering as `deepsight-polaris-software`: an interface header per module, the
platform-specific implementation in a subdirectory under it, and one `.md` per module describing
what it owns and what it is not allowed to do.

```
src/
├── main.cpp                    hands control to app::App and does nothing else
├── config.hpp                  every Kconfig value, in one place
├── app/                        high-level logic and the device state machine
│   ├── app.cpp/.hpp            the App singleton: initialize(), run()
│   ├── app_config.hpp          timeouts and retry limits
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
│   ├── idle_hook/              callback registry for idle time
│   ├── port/                   how a module is addressed
│   ├── state_machine/          flat state machine
│   └── timer/                  posts an event on expiry
├── hal/                        hardware abstraction
│   ├── ble/       IBle       + BleFactory      + zephyr/
│   ├── gpio/      IGpio      + ManagerFactory  + zephyr/
│   ├── led/       Led        + Manager         (no platform code: goes through IGpio)
│   ├── link/      ILink      + LinkFactory     + lora/ and tcp/, one compiled
│   ├── os/        Thread/Queue/Timer            + zephyr/
│   ├── system/    free functions                + zephyr/
│   └── watchdog/  IWatchdog  + WatchdogFactory + zephyr/
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

**`hal::link::send()` has exactly one caller, `svc::comms`.** Two contexts writing to the same SPI
peripheral corrupt each other quietly rather than failing loudly, so this is the constraint most
worth respecting. `svc::system_diagnostics` reads link status and never transmits.

Both are explained in `docs/ARCHITECTURE.md` section 4, and each module's `.md` restates the part
that applies to it.

## Hardware

Both variants are the same nRF52840 DK with a different module on the same four SPI pins, so only
one can be plugged in at a time.

### LoRa variant: Modtronix inAir9 (SX1276)

The plain one with the RFO output. The inAir9B is the +20 dBm variant and needs
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

### TCP variant: Wiznet W5500

Same SPI pins, plus an interrupt and a reset line.

| W5500 | DK header | nRF52840 pin |
| --- | --- | --- |
| MISO | D12 | P1.14 |
| MOSI | D11 | P1.13 |
| SCLK | D13 | P1.15 |
| SCS | D10 | P1.12 |
| INT | D2 | P1.03 |
| RSTn | D9 | P1.11 |
| 3V3 | 3V3 | |
| GND | GND | |

**These have not been verified against a generated devicetree**, unlike the inAir9 ones above: no
W5500 has been wired up yet. Check `build/zephyr/zephyr.dts` before trusting them. A header index
that shifts does not fail the build, it produces a controller that never answers.

`snippets/eth-w5500/w5500.overlay` is where they live.

## Build

Three variants build from this repository. All three have been built against **Zephyr v4.4.1**
with **Zephyr SDK 1.0.1**:

| variant | board | flash | RAM |
| --- | --- | --- | --- |
| LoRaWAN (default) | nRF52840 DK + inAir9 | 137184 B / 1 MB (13.08%) | 56112 B / 256 KB (21.41%) |
| TCP | nRF52840 DK + W5500 | 137936 B / 1 MB (13.15%) | 69520 B / 256 KB (26.52%) |
| Wi-Fi | ESP32-S3-DevKitC-1 | 653876 B / 8 MB (7.80%) | 250112 B / 390 KB DRAM (62.67%) |

The ESP32 figure is the one to watch, and it is DRAM rather than flash: about 80 KB of that is the
system heap Espressif's Wi-Fi and Bluetooth drivers declare. See the comment in `prj.conf`.

### The short way

On Windows, `tools/` has a script per variant. Each one activates the workspace virtualenv, moves
to the repository root and runs the right `west` invocation, so none of the flags below have to be
remembered:

```
tools\lora-build.bat            tools\wifi-build.bat
tools\lora-flash.bat            tools\wifi-flash.bat
                                 tools\wifi-monitor.bat
```

Add `pristine` to either build script to build from scratch, which is what a change to `Kconfig`,
`prj.conf` or a devicetree overlay usually needs:

```
tools\lora-build.bat pristine
```

`tests/utils/uplink_server.py` is the other end of the TCP and Wi-Fi variants: run it on a machine
the concentrator can reach and it decodes and prints every uplink, in values or raw bytes.
`tests/README.md` covers it, and `tests/pytest` tests the decoder with no hardware attached.

The rest of this section is what those scripts do and why, which is worth reading once.

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
D:\ZephyrWS\
├── .west\                                  created by west init
├── base_platform_zephyr_ble_concentrator\  this repo, and the manifest
├── zephyr\                                 fetched by west update
└── modules\                                fetched by west update
```

`west.yml` uses `import: name-allowlist:` rather than a bare `import: true`, so `west update`
fetches twelve modules instead of every vendor HAL Zephyr knows about. That is the difference
between a 5 GB workspace and a 9 GB one. Adding a board from a new vendor means adding its HAL to
that list.

### One-time setup (Windows)

Host tools, once, from an elevated PowerShell:

```powershell
winget install Kitware.CMake Ninja-build.Ninja oss-winget.gperf oss-winget.dtc Git.Git Python.Python.3.12 7zip.7zip
```

Workspace and west:

```powershell
mkdir D:\ZephyrWS
cd D:\ZephyrWS
py -3.12 -m venv .venv
.venv\Scripts\activate
pip install west

git clone https://github.com/pgomezm/base_platform_zephyr_ble_concentrator.git
west init -l base_platform_zephyr_ble_concentrator
west update                       # fetches Zephyr and its modules, slow the first time
west zephyr-export
pip install -r zephyr\scripts\requirements.txt
west config build.dir-fmt "build/{board}/{app}"
```

That last line gives each board-and-application pair its own build directory, so switching between
the two variants does not silently reuse the other one's CMake cache.

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
mkdir ZephyrWS && cd ZephyrWS
python3 -m venv .venv && source .venv/bin/activate
pip install west
git clone https://github.com/pgomezm/base_platform_zephyr_ble_concentrator.git
west init -l base_platform_zephyr_ble_concentrator
west update && west zephyr-export
pip install -r zephyr/scripts/requirements.txt
west sdk install -t arm-zephyr-eabi
west config build.dir-fmt "build/{board}/{app}"
```

Also needed on the host: `cmake >= 3.20`, `ninja`, `gperf`, `dtc` (device-tree-compiler).

### The two variants

The concentrator ships in two variants that differ only in how collected
readings leave the device. Everything above `src/hal/link` is identical, so this
is one repository with two builds rather than two firmwares.

| variant | hardware on the SPI header | selected by |
| --- | --- | --- |
| LoRaWAN (default) | Modtronix inAir9 (SX1276) | `CONFIG_APP_LINK_LORA` |
| TCP | Wiznet W5500 | `CONFIG_APP_LINK_TCP` plus the `eth-w5500` snippet |

Both cannot be plugged in at once: they are the same four SPI pins, which is why
the `eth-w5500` snippet deletes the SX1276 node the board overlay declares.

The server address, port, and static-versus-DHCP addressing are Kconfig symbols
under "Uplink transport", consumed through `src/config.hpp`. Changing where
uplinks go is a `prj.conf` line or a `menuconfig` field, never a source edit.

### Build and flash

From the workspace root, with the venv active:

```
west build -b nrf52840dk/nrf52840 base_platform_zephyr_ble_concentrator --pristine
west flash
```

The TCP variant, which builds with no Ethernet hardware present:

```
west build -b nrf52840dk/nrf52840 base_platform_zephyr_ble_concentrator --pristine -S eth-w5500 -- -DCONFIG_APP_LINK_TCP=y
```

Keeping that build green from the start is what stops the TCP path from rotting
while the hardware is still being chosen. It will not *run* without a network
interface: `hal::link::initialize()` reports `NOT_READY` when
`net_if_get_default()` returns nothing.

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
west build -b nrf52840dk/nrf52840 base_platform_zephyr_ble_concentrator -p always     # force clean
```

### One backend per seam, chosen at build time

Two modules have more than one possible implementation, and in both cases
exactly one is compiled: `src/hal/link` picks LoRa or TCP from the Kconfig
choice above, and `src/hal/os` has only its Zephyr backend today. A FreeRTOS
`hal::os` backend was written and then removed — it could not be compiled here,
so its assertions were unverifiable and sizing the shared buffers for it cost
320 B of RAM that nothing used. See `src/hal/os/os.md` for what adding a second
one would actually involve.

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

**Neither transport carries data yet.** The BLE half — passive scan, Eddystone parsing, the device
table, the state machine, the watchdog, the LEDs — is fully exercised. Everything downstream of
`hal::link` is not.

**LoRaWAN: the join is not implemented.** `connect()` logs a warning and returns `CONNECT_ERROR`,
because whether this device uses OTAA or ABP, and against which network server, has not been
decided. It fails deliberately rather than pretending to succeed, so the state machine takes its
error path instead of the firmware believing it is connected.

Filling it in is a contained change in `src/hal/link/lora/link_lora.cpp` plus the credentials. Note
that Zephyr's stack does **not** retry a failed join: `lorawan_join()` attempts once and returns an
errno, so retrying stays this firmware's job — which is what the `SOFT_ERROR` path already does.

**TCP: never run against hardware.** It compiles in both the static-address and DHCP
configurations, and no W5500 has been connected to check that it works. Downlink reception is
accepted but never delivered: that would need a reader thread, and framing over a stream is
undecided.

The consequence when you flash either variant today: it comes up, scans, collects readings into the
device table, and then cycles through `STARTUP` to `SOFT_ERROR` when it tries to dispatch.

Open items are listed in `docs/ARCHITECTURE.md` section 9.
