#!/usr/bin/env python3
"""Open the device console, finding the port instead of asking for it.

    python tests/utils/console.py            # find the board and attach
    python tests/utils/console.py --list     # just show what is connected
    python tests/utils/console.py --port COM17 --baud 115200

Ctrl-] closes it.

This does not go through ``west espressif monitor``. That command wants a build
directory and disagrees with this repository's build.dir-fmt about where one is,
and it only covers the Espressif boards anyway. pyserial is already in the
workspace virtualenv, works for both boards, and has no opinion about builds.

The port number is not stable: Windows assigns a different COM depending on
which socket the board is plugged into, and the nRF52840 DK enumerates three at
once. Looking it up in Device Manager every time is the thing this replaces.
"""

from __future__ import annotations

import argparse
import sys

from serial.tools import list_ports, miniterm


def enable_ansi_colours() -> None:
    """Let the Windows console interpret the escape codes Zephyr sends.

    Zephyr's log backend colours errors red and warnings yellow with ANSI
    sequences. A console that does not interpret them prints the sequences
    themselves - the `ESC[0m` litter that makes a log unreadable.

    Windows 10 and 11 can do it, but not by default: the terminal starts with
    virtual-terminal processing off and a program has to ask. This asks.

    Silent on failure on purpose. It is a nicety, and a console that refuses is
    not a reason to fail to open the port. If it does refuse, the alternative is
    to stop sending the codes at all, with
    CONFIG_LOG_BACKEND_SHOW_COLOR=n in the build.
    """
    if sys.platform != "win32":
        return

    try:
        import ctypes

        kernel32 = ctypes.windll.kernel32
        handle = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE

        # ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT
        # | ENABLE_VIRTUAL_TERMINAL_PROCESSING
        kernel32.SetConsoleMode(handle, 0x0001 | 0x0002 | 0x0004)
    except Exception:
        pass

#: USB vendor ids, most specific first. The first match wins, so a board plugged
#: in alongside a USB-serial adapter still gets picked correctly.
KNOWN_VENDORS = [
    (0x303A, "Espressif USB-Serial-JTAG"),   # ESP32-S3 native USB, the `USB` socket
    (0x1366, "SEGGER J-Link"),               # nRF52840 DK, the CDC UART port
    (0x10C4, "Silicon Labs CP210x"),         # the DevKitC's `UART` socket
    (0x1A86, "WCH CH340"),                   # some clone boards
]


def describe(port) -> str:
    vid = f"{port.vid:04X}" if port.vid is not None else "----"
    pid = f"{port.pid:04X}" if port.pid is not None else "----"
    return f"{port.device:<8} {vid}:{pid}  {port.description}"


def find_port():
    """Return the most likely device port, or None.

    A J-Link presents several interfaces and the console is on the lowest
    numbered one, which is why the candidates are sorted before choosing.
    """
    ports = list(list_ports.comports())

    for vid, name in KNOWN_VENDORS:
        matches = sorted((p for p in ports if p.vid == vid), key=lambda p: p.device)
        if matches:
            return matches[0], name

    return None, None


def main() -> int:
    parser = argparse.ArgumentParser(description="Attach to the device console.")
    parser.add_argument("--port", help="serial port, e.g. COM17 (default: find it)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="baud rate (default: 115200)")
    parser.add_argument("--list", action="store_true",
                        help="list the serial ports and exit")
    args = parser.parse_args()

    if args.list:
        ports = list(list_ports.comports())
        if not ports:
            print("no serial ports at all - check the cable and which USB socket "
                  "on the board it is in")
            return 1
        for port in sorted(ports, key=lambda p: p.device):
            print(describe(port))
        return 0

    port = args.port
    if port is None:
        found, name = find_port()
        if found is None:
            print("no known board found. Ports seen:", file=sys.stderr)
            for p in sorted(list_ports.comports(), key=lambda x: x.device):
                print("  " + describe(p), file=sys.stderr)
            print("\nPass --port explicitly, or check the cable carries data and "
                  "which USB socket on the board it is in.", file=sys.stderr)
            return 1
        port = found.device
        print(f"-- {name} on {port} ({found.description})")

    enable_ansi_colours()

    print(f"-- {port} at {args.baud}, Ctrl-] to exit")

    # miniterm reads its arguments from sys.argv, so hand it a clean one.
    sys.argv = ["miniterm", port, str(args.baud)]
    miniterm.main()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
