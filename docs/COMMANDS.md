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