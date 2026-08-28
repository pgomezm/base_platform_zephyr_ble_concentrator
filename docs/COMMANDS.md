# Commands

Every Python tool in this repository, what it is for, and how to run it.

All of them are run **from the repository root** with the workspace virtualenv active.

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
python build_flash_tools\run_build_tool.py --variant lora
```

| flag | values | default |
| --- | --- | --- |
| `--variant` | `lora`, `tcp`, `wifi` | `lora` |
| `--action` | `build`, `clean`, `clean_build` | `build` |
| `--desc` | free text added to the artefact name | none |
| `--log` | `debug`, `info`, `warning`, `error`, `critical` | `info` |

```sh
python build_flash_tools\run_build_tool.py --variant wifi --action clean_build
python build_flash_tools\run_build_tool.py --variant lora --desc bench
```

The result is copied into `output/` under a name carrying the version, the commit and — when the
tree has uncommitted changes — a `-dirty` marker. A `-dirty` artefact cannot be rebuilt from any
commit, so do not ship one.

## Flash

```sh
python build_flash_tools\run_flash_tool.py --variant lora
```

| flag | what |
| --- | --- |
| `--variant` | `lora`, `tcp`, `wifi` |
| `--file` | flash this artefact instead of the one in the build directory |
| `--log` | log level |

`--file` is what `output/` exists for: putting back exactly what was on a board last week without
rebuilding it and hoping.

```sh
python build_flash_tools\run_flash_tool.py --variant lora --file output\concentrator-lora_0.1.0-dev.a1b2c3d4.hex
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

The other end of the TCP and Wi-Fi variants. Listens, decodes and prints what the concentrator
sends.

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
python build_flash_tools\run_build_tool.py --variant lora --action clean_build
python build_flash_tools\run_flash_tool.py --variant lora
python tests\utils\console.py
```

Start `mosquitto_sub -v -t 'lora/#'` on the gateway before the board rejoins, or the join scrolls
past unobserved.

**Wi-Fi, end to end:**

```sh
python build_flash_tools\run_build_tool.py --variant wifi
python build_flash_tools\run_flash_tool.py --variant wifi
python tests\utils\uplink_server.py --format both
```

and the console in a second window if you want to watch the association.

**Before committing:**

```sh
python build_flash_tools\run_format_tool.py --check
pytest tests\pytest
python build_flash_tools\run_build_tool.py --variant lora
python build_flash_tools\run_build_tool.py --variant wifi
```

Both variants, because they compile different files: `link_lora.cpp` on one, `socket_link.cpp` plus
`link_wifi.cpp` on the other. A change that breaks only one is easy to miss.
---

## Debugging

A debug build is a different Kconfig, so it gets its own directory and leaves the release build
alone:

```sh
python build_flash_tools\run_build_tool.py --variant lora --debug
python build_flash_tools\run_flash_tool.py --variant lora --debug
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
