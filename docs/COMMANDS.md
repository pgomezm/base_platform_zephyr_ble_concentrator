# Commands

Every Python tool in this repository, what it is for, and how to run it.

## The pieces, and what each one is called

The names below are used consistently in this document, in the code and in the logs. A bench has
three parts, and "it does not work" usually means one of them is missing rather than broken.

| name | what it is | where it lives |
| --- | --- | --- |
| **endpoint** | the sensor. Advertises its reading over BLE and nothing else — it never connects, never listens, does not know the concentrator exists | its own firmware, `base_platform_baremetal_ble`. Not this repository |
| **concentrator** | what this repository builds. Scans for endpoints, keeps the last reading of each one, and forwards them upstream on a timer | the board |
| **board** | the hardware the concentrator runs on: nRF52840 DK for the `lora` and `tcp` configs, ESP32-S3 DevKitC for `wifi` | on the desk |
| **uplink** | one message from the concentrator upstream, carrying the readings collected since the last one. Fragmented when it does not fit | on the wire |
| **link** | the code that puts an uplink on the wire. One is compiled in per build | `hal/link/` |
| **config** | which of the three builds: `lora`, `tcp` or `wifi`. Picks the link, the board and the Kconfig overlay together, and every tool takes it as `--config`. Not to be confused with Zephyr's `CONFIG_*` symbols | `BUILD_CONFIGS` in `run_build_tool.py` |
| **uplink server** | the other end for the `tcp` and `wifi` configs. Listens, decodes and prints. Runs on the PC | `tests/utils/uplink_server.py` |
| **console** | the concentrator's own serial log. Says what the firmware thinks is happening | `tests/utils/console.py` |

The chain, end to end:

```
endpoint  --BLE advert-->  concentrator  --uplink-->  upstream
 (sensor)                    (board)                   |
                                                       +-- wifi / tcp : uplink_server.py on the PC
                                                       +-- lora       : LoRaWAN gateway -> network
                                                                        server -> MQTT broker
```

The `lora` config has no uplink server: its upstream is a LoRaWAN gateway, and the readings come
out at an MQTT topic on the other side of the network server. `uplink_server.py` is only for the
two IP links.

---

## Before anything

All of the tools are run **from the repository root** with the workspace virtualenv active.

```
D:\ZephyrWS\.venv\Scripts\activate
cd D:\ZephyrWS\base_platform_zephyr_ble_concentrator
```

Your prompt shows `(.venv)` when it is on. Without it the Wi-Fi build fails on a missing `esptool`,
because that lives in the virtualenv and not in the system Python. The build tool now says so
instead of letting CMake fail twenty seconds later.

---

## Build

```sh
python build_flash_tools\run_build_tool.py --config lora
```

| flag | values | default |
| --- | --- | --- |
| `--config` | `lora`, `tcp`, `wifi` | `lora` |
| `--action` | `build`, `clean`, `clean_build` | `build` |
| `--desc` | free text added to the artefact name | none |
| `--log` | `debug`, `info`, `warning`, `error`, `critical` | `info` |

```sh
python build_flash_tools\run_build_tool.py --config wifi --action clean_build
python build_flash_tools\run_build_tool.py --config lora --desc bench
```

The result is copied into `output/` under a name carrying the version, the commit and — when the
tree has uncommitted changes — a `-dirty` marker. A `-dirty` artefact cannot be rebuilt from any
commit, so do not ship one.

## Flash

```sh
python build_flash_tools\run_flash_tool.py --config lora
```

| flag | what |
| --- | --- |
| `--config` | `lora`, `tcp`, `wifi` |
| `--file` | flash this artefact instead of the one in the build directory |
| `--log` | log level |

`--file` is what `output/` exists for: putting back exactly what was on a board last week without
rebuilding it and hoping.

```sh
python build_flash_tools\run_flash_tool.py --config lora --file output\concentrator-lora_0.1.0-dev.a1b2c3d4.hex
```

The board still needs its build directory present — the runner reads the chip and the addresses
from it. Only the image comes from elsewhere.

## Format

```sh
python build_flash_tools\run_format_tool.py            # reformat in place
python build_flash_tools\run_format_tool.py --check    # report only, change nothing
```

`--check` is the one to run before committing. It exits non-zero if anything is unformatted, so it
also works as a gate in a script.

---

## Console

One command for both boards. It finds the port by USB vendor id, so there is no COM number to look
up in Device Manager.

```sh
python tests\utils\console.py
```

| flag | what |
| --- | --- |
| `--port` | say which one, e.g. `COM17` |
| `--baud` | default 115200 |
| `--list` | show what is connected and exit |

With **both boards plugged in at once** it refuses to guess: it lists them and asks for `--port`.

`Ctrl-]` closes it.

## Uplink server

The other end of the `tcp` and `wifi` configs. It listens on the PC, and the concentrator is the
one that opens the connection outward — so no inbound port has to be forwarded, but the server does
have to be up **before** the concentrator tries to connect. See the note under the Wi-Fi sequence.

```sh
python tests\utils\uplink_server.py
```

| flag | values | default |
| --- | --- | --- |
| `--host` | address to bind | `0.0.0.0` |
| `--port` | port to listen on | `5000` |
| `--format` | `values`, `raw`, `both` | `values` |
| `--csv PATH` | also append every record to a CSV | none |
| `--once` | serve one connection and exit | off |

```sh
python tests\utils\uplink_server.py --format both
python tests\utils\uplink_server.py --csv bench.csv
```

`--format raw` prints the bytes as they arrived, which is what to use when the decoding itself is
in question. `values` is the decoded reading per device.

## Unit tests

```sh
pytest tests\pytest
```

`pytest.ini` turns live logging on, so output appears as the tests run rather than at the end.

`conftest.py` adds `--host` and `--port` for tests that need a live device. Nothing uses them yet;
they are there so a hardware test has somewhere to read its bench parameters from.

```sh
pytest tests\pytest --host 192.168.1.50 --port 5000
```

---

## The usual sequences

**LoRa, from scratch, watching it join:**

```sh
python build_flash_tools\run_build_tool.py --config lora --action clean_build
python build_flash_tools\run_flash_tool.py --config lora
python tests\utils\console.py
```

Start `mosquitto_sub -v -t 'lora/#'` on the gateway before the board rejoins, or the join scrolls
past unobserved.

**Wi-Fi, end to end** — all three parts of the chain, in this order:

1. At least one **endpoint** powered and advertising. Nothing arrives without one, and the
   concentrator will look healthy the whole time: it joins the network and dispatches an empty
   uplink every period.
2. The **uplink server** on the PC, left running in its own window:

   ```sh
   python tests\utils\uplink_server.py --format both
   ```

3. The **concentrator** built, flashed and then **reset**:

   ```sh
   python build_flash_tools\run_build_tool.py --config wifi
   python build_flash_tools\run_flash_tool.py --config wifi
   ```

The order matters, and step 3 is where it bites. If the server is not listening when the
concentrator comes up, the connect fails, and the state machine retries five times and stops in
`HARD_ERROR` — terminal, both LEDs blinking, nothing gets it out except a reset. Starting the
server afterwards does not revive it.

The console in a fourth window if you want to watch the association and the dispatches:

```sh
python tests\utils\console.py
```

`--format both` prints the decoded values and the bytes they came from side by side. That is the
one to use the first time an endpoint is tried: if the raw arrives but the values are zero, the
problem is this side's parsing, not the air.

**Before committing:**

```sh
python build_flash_tools\run_format_tool.py --check
pytest tests\pytest
python build_flash_tools\run_build_tool.py --config lora
python build_flash_tools\run_build_tool.py --config wifi
```

Both configs, because they compile different files: `link_lora.cpp` on one, `socket_link.cpp` plus
`link_wifi.cpp` on the other. A change that breaks only one is easy to miss.

---

## Debugging

A debug build is a different Kconfig, so it gets its own directory and leaves the release build
alone:

```sh
python build_flash_tools\run_build_tool.py --config lora --debug
python build_flash_tools\run_flash_tool.py --config lora --debug
```

`--debug` applies `prj_debug.conf` and builds into `build\lora-debug`. It is deliberately **not**
filed into `output/`: a debug binary is for a bench, and one sitting next to the release artefacts
is one that gets flashed by mistake six weeks later.

What the overlay turns on:

| | why |
| --- | --- |
| `CONFIG_DEBUG_OPTIMIZATIONS` | `-Og` instead of `-Os`, so stepping does not jump backwards and locals are not `<optimized out>` |
| `CONFIG_DEBUG` | also defines `APP_DEBUG_BUILD`, which is what makes `ASSERT_CRITICAL` real |
| `CONFIG_DEBUG_THREAD_INFO` | the debugger lists threads by name |
| `CONFIG_ASSERT` | Zephyr's own assertions, off by default |
| `CONFIG_THREAD_ANALYZER` | prints real stack usage per thread every 30 s, by name |
| `CONFIG_APP_LOG_LEVEL_DBG` | `LOG_DEBUG()` starts coming out |

Every symbol in the overlay has to exist on **both** boards: Zephyr aborts on a Kconfig warning, so
one that is only defined for Arm takes the Wi-Fi build down with it. The tail of `prj_debug.conf`
lists what was left out and why.

The thread analyser is worth running once on its own account: every active object uses
`Thread::STACK_SIZE = 2048`, and that number was chosen by copying, never measured.

### Setting it up on a new machine

`launch.json` and `tasks.json` name no path of their own. Everything they need comes from
`.vscode/settings.json`, which is **not** committed. Copy the template and edit it:

```sh
copy .vscode\settings.json.example .vscode\settings.json
```

Eight paths: the workspace virtualenv's `python` and `pytest`, the Zephyr SDK's Arm GDB and its
`bin` directory, the SEGGER J-Link GDB server, the Zephyr SDK's Xtensa GDB, and Espressif's OpenOCD
with its scripts directory. Forward slashes, on Windows too — a backslash in JSON is an escape.

If one is wrong VS Code says which setting it could not resolve, which is a better failure than the
`ENOENT` you get from a hardcoded path that moved.

### From VS Code

`.vscode/launch.json` has the configurations. Needs the **Cortex-Debug** extension and the SEGGER
J-Link software, which the DK's onboard debugger speaks.

Pick *LoRa (nRF52840): flash and debug* and press F5 — it builds, flashes and stops at `main`.
*attach to a running board* connects to one already running, without resetting it.

`.vscode/tasks.json` has everything else as tasks: build, flash, console, uplink server, format and
pytest, all from the command palette, all calling the same Python tools a terminal would.

**The ESP32-S3 is Xtensa, not ARM**, so Cortex-Debug cannot drive it, and as of today debugging it
does not work at all here. See below.

### From a terminal

```sh
west debug -d build\lora-debug        # start, halted at main
west attach -d build\lora-debug       # connect to a board already running
```

### The ESP32-S3: connects, but breakpoints do not hold

Most of the way there. What works, and where it stops.

**Working.** OpenOCD talks to the chip over the built-in USB-JTAG, GDB connects, symbols resolve —
it reports the source file and line it is halted at — and the target resets and runs.

**Not working.** Breakpoints never hit.

#### Getting to where it is now

1. `openocd-esp32` from Espressif's GitHub releases, unpacked at `<workspace>/tools/openocd-esp32`.
   Not `west espressif install`, which fails with `could not find build configuration` because it
   expects west's default `build/` layout.
2. `run_build_tool.py` passes `ESPRESSIF_TOOLCHAIN_PATH`, `OPENOCD` and `OPENOCD_DEFAULT_PATH` into
   the Wi-Fi build. `ESPRESSIF_TOOLCHAIN_PATH` is a CMake variable, not an environment one, and the
   Zephyr SDK toolchain never sets it.
3. **The USB driver.** Windows installs its own WinUSB through WCID, and libusb cannot open it:
   `LIBUSB_ERROR_NOT_FOUND`. Zadig, Options → List All Devices,
   `USB JTAG/serial debug unit (Interface 2)` — interface **2**, not 0, which is the CDC serial port
   you flash through — then Downgrade WCID Driver.

Verify with OpenOCD alone before involving VS Code:

```sh
tools\openocd-esp32\bin\openocd.exe -s tools\openocd-esp32\share\openocd\scripts -f <zephyr>\boards\espressif\esp32s3_devkitc\support\openocd.cfg
```

`Listening on port 3333 for gdb connections` means the hard part is done.

#### The breakpoint problem

OpenOCD cannot read a partition table, because a Zephyr image starts at 0x0 and is not laid out
like an ESP-IDF one:

```
Warn : Failed to get flash maps, result code 0x4!
Warn : Unknown magic number in partition table!
```

Without it, OpenOCD hands GDB a memory map that describes the code region as R/W. GDB concludes it
is RAM and asks for a software breakpoint, which means writing a break instruction into flash:

```
Warn : address 0x42007a90 not writable
Error: Failed to write breakpoint instruction (-4)!
```

`monitor gdb_breakpoint_override hard` in `postRemoteConnectCommands` makes OpenOCD use hardware
breakpoints regardless. That removes the error — and the breakpoints still do not hit. That is
where this stands, and the reason is not yet known.

Also tried and abandoned: a temporary breakpoint at `main` in `postRemoteConnectCommands`, which
does not survive the ROM and second-stage bootloaders and took the session down with it.

The ESP32-S3 has **two** hardware breakpoints. Whatever comes next has to fit in two.

#### Whether to keep going

Nothing that is ESP32-only is worth much stepping. `eda/`, the four services, the fragmentation and
the state machine are the same code on both boards and already debuggable on the nRF52840. The one
file that exists only here is `hal/link/wifi/link_wifi.cpp`, and its interesting parts are network
callbacks where halting changes the timing and the log tells you more.

When a bug turns up that only happens on the S3, this is where to start — and the first question to
answer is whether the firmware is running at all: open the console next to the debug session. Log
output means the CPU is running and only GDB's view is wrong; silence means it never started.

### After a crash

Zephyr's default fatal handler halts rather than reboots, so the register dump stays on the
console. To turn a program counter into a source line:

```sh
arm-zephyr-eabi-addr2line -e build\lora-debug\zephyr\zephyr.elf 0x0002a1f4
```

---

## When something goes wrong

**`esptool>=5.0.2 not found in PATH`** — the virtualenv is not active. Activate it. Do not install
esptool system-wide; that leaves two environments, one of which works by accident.

**`No build at ...\build\wifi`** — flashing before building. Build first.

**The console finds nothing** — `--list` shows what is connected. The nRF52840 DK enumerates three
ports and the console is on the lowest-numbered one, which the tool already knows.

**Both boards plugged in and the console refuses to pick** — that is deliberate. Pass `--port`.

**`fatal: Unable to create '.git/index.lock'`** — a stale lock, not a running git. Delete
`.git\index.lock` and retry.

**Stepping jumps backwards, locals show `<optimized out>`** — a release build. Rebuild with
`--debug`.

**`connect to <address>:5000 failed (116)`** — 116 is `ETIMEDOUT`. Nobody is listening. Either the
uplink server is not running, or the address compiled into the build is not the PC's current one
(`ipconfig`), or Windows Firewall blocked Python's first attempt to open the port and never asked
again. Give the PC a fixed address on the router; a DHCP lease that moves breaks this every few
weeks.

**Both LEDs blinking together** — `HARD_ERROR`. The device gave up and is waiting to be looked at,
by design: it does not reboot itself, because rebooting erases what caused the fault. The console
says which fault. Reset once the cause is fixed.

**The uplink server prints nothing, but the console says the concentrator is dispatching** — the
concentrator is up and the link works; there is just no endpoint in range, or its advertisement is
not being recognised. Check the company id and the manufacturer data on both sides.
