'''
Build a concentrator variant and file the result under output/.

    python build_flash_tools/run_build_tool.py --variant lora
    python build_flash_tools/run_build_tool.py --variant wifi --action clean_build
    python build_flash_tools/run_build_tool.py --variant tcp --desc bench

Reads the version out of src/version.h, takes the git commit hash, builds,
then copies the artefact into output/ under a name that says what it is.

The reason the copy exists at all: a build directory holds exactly one
zephyr.hex and the next build overwrites it. Weeks later, "what is actually on
that board" has no answer. A file called

    concentrator-lora_0.1.0-dev.4f2a91c3.hex

answers it.
'''

import argparse
import logging
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from log_tool import setup_logger  # noqa: E402

logger = logging.getLogger(__name__)

PROJECT_ROOT = Path(__file__).resolve().parents[1]

#: Where a debug probe's tools are unpacked, if they are. The Espressif build
#: looks for OpenOCD in exactly one place and nowhere else, and that place is a
#: CMake variable the Zephyr SDK toolchain never sets, so this passes it.
#:
#: Absent is fine: the arguments are only added when the directory is there, so
#: a clone without it still builds and only loses the ability to debug.
TOOLS_ROOT = PROJECT_ROOT.parent / "tools"
OUTPUT_DIR = PROJECT_ROOT / "output"
VERSION_FILE = PROJECT_ROOT / "src" / "version.h"

#: One entry per build this repository produces. The board, whatever has to be
#: passed to select the transport, and which file is the deliverable.
#:
#: The Wi-Fi build needs no transport flag: its board conf sets
#: CONFIG_APP_LINK_WIFI, because that board has neither an SX127x nor a wired
#: interface and building it any other way is a mistake rather than a choice.
VARIANTS = {
    "lora": {
        "board": "nrf52840dk/nrf52840",
        "snippet": None,
        "cmake_args": ["-DBOARD_FLASH_RUNNER=jlink"],
        "artefact": "zephyr.hex",
    },
    "tcp": {
        "board": "nrf52840dk/nrf52840",
        # Declares the W5500 in the devicetree, so the build has a network
        # interface to compile against with no hardware attached.
        "snippet": "eth-w5500",
        "cmake_args": ["-DCONFIG_APP_LINK_TCP=y", "-DBOARD_FLASH_RUNNER=jlink"],
        "artefact": "zephyr.hex",
    },
    "wifi": {
        "board": "esp32s3_devkitc/esp32s3/procpu",
        "snippet": None,
        "cmake_args": [],
        "artefact": "zephyr.bin",
        # Espressif's build step shells out to this. It lives in the workspace
        # virtualenv, so a console that never activated it fails here.
        "needs_on_path": ["esptool"],
        # And debugging needs Espressif's OpenOCD, which is not the generic one.
        "needs_openocd_esp32": True,
    },
}


def put_virtualenv_on_path() -> None:
    """Make the interpreter's own scripts reachable by name.

    Running .venv/Scripts/python.exe directly is a normal way to use a
    virtualenv, and it is what a VS Code task does. It sets sys.prefix but not
    PATH, so `esptool` is installed and still not findable - and CMake looks for
    it on PATH.

    Activating fixes it too, but requiring activation makes the tools work from
    one console and not from another for no reason a caller can see. This does
    the same thing without asking.
    """
    scripts = Path(sys.prefix) / ("Scripts" if os.name == "nt" else "bin")

    if not scripts.is_dir():
        return

    path = os.environ.get("PATH", "")

    if str(scripts).lower() in [entry.lower() for entry in path.split(os.pathsep)]:
        return

    os.environ["PATH"] = str(scripts) + os.pathsep + path
    logger.debug("Added %s to PATH", scripts)


def check_prerequisites(variant: str) -> None:
    """Fail now, with the real reason, rather than inside CMake.

    Forgetting to activate the workspace virtualenv surfaces twenty seconds
    into a build as "esptool>=5.0.2 not found in PATH", which reads like a
    missing package and invites installing it system-wide. It is almost always
    the virtualenv.

    :param variant: which build is about to run
    :raises RuntimeError: if something the build needs is missing
    """
    in_virtualenv = sys.prefix != sys.base_prefix

    for command in VARIANTS[variant].get("needs_on_path", []):
        if shutil.which(command) is not None:
            continue

        if in_virtualenv:
            raise RuntimeError(
                f"{command} is not on PATH. The active virtualenv is "
                f"{sys.prefix}; install it there with: pip install {command}")

        raise RuntimeError(
            f"{command} is not on PATH, and no virtualenv is active. "
            f"Activate the workspace one and try again.")

    if not in_virtualenv:
        logger.warning("No virtualenv is active; building with %s", sys.executable)


def parse_args():
    parser = argparse.ArgumentParser(description="Build a concentrator variant.")
    parser.add_argument("--variant", choices=sorted(VARIANTS), default="lora",
                        help="which build to produce (default: lora)")
    parser.add_argument("--action", choices=["build", "clean", "clean_build"], default="build",
                        help="incremental build, remove the build directory, or both")
    parser.add_argument("--debug", action="store_true",
                        help="apply prj_debug.conf and build into build/<variant>-debug")
    parser.add_argument("--desc", default=None,
                        help="label to add to the artefact name, e.g. a bench or a site")
    parser.add_argument("--log", choices=["debug", "info", "warning", "error", "critical"],
                        default="info", help="verbosity")
    return parser.parse_args()


def build_dir(variant: str, debug: bool = False) -> Path:
    """One directory per variant, and a separate one for its debug build.

    A debug build is a different Kconfig, so sharing a directory with the
    release build would mean a full reconfigure every time you switch. Keeping
    them apart costs disk and saves waiting.

    Explicit rather than left to west's build.dir-fmt, which keys on the board:
    the LoRa and TCP builds share a board and would overwrite each other, and
    they do not even share a devicetree - the TCP snippet deletes the SX127x
    node the board overlay declares.
    """
    return PROJECT_ROOT / "build" / (variant + "-debug" if debug else variant)


def get_git_commit_hash() -> str:
    """First 8 characters of HEAD, with a marker when the tree is dirty.

    The marker is the point. A binary built from uncommitted work, labelled with
    a clean commit hash, is a file that lies about what is in it - and it lies
    exactly when it matters, which is when something is wrong and the hash is
    what you are trusting.
    """
    try:
        commit = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=PROJECT_ROOT).decode("utf-8").strip()[:8]
    except Exception:
        logger.error("Failed to get the git commit hash.")
        return "nogit"

    try:
        dirty = subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=PROJECT_ROOT).decode("utf-8").strip()
    except Exception:
        dirty = ""

    if dirty:
        logger.warning("Working tree is dirty: this artefact is not reproducible from %s", commit)
        return commit + "-dirty"

    return commit


def get_firmware_version(desc: str = None) -> str:
    """Read VER_MAJOR/MINOR/PATCH and RELEASE_TAG out of src/version.h."""
    fields = {}

    try:
        with open(VERSION_FILE, "r", encoding="utf-8") as handle:
            for line in handle:
                match = re.match(r"#define\s+(VER_MAJOR|VER_MINOR|VER_PATCH)\s+(\d+)", line)
                if match:
                    fields[match.group(1)] = match.group(2)

                match = re.match(r'#define\s+RELEASE_TAG\s+"([^"]*)"', line)
                if match:
                    fields["RELEASE_TAG"] = match.group(1)
    except Exception as error:
        logger.error("Failed to read %s: %s", VERSION_FILE, error)
        return "unknown"

    missing = {"VER_MAJOR", "VER_MINOR", "VER_PATCH"} - set(fields)
    if missing:
        logger.error("%s is missing %s", VERSION_FILE, ", ".join(sorted(missing)))
        return "unknown"

    version = f"{fields['VER_MAJOR']}.{fields['VER_MINOR']}.{fields['VER_PATCH']}"

    tag = fields.get("RELEASE_TAG", "")
    if tag:
        version += f"-{tag}"

    if desc:
        version += f"-{desc}"

    return version


def run_clean(variant: str, debug: bool = False) -> None:
    target = build_dir(variant, debug)

    if target.exists():
        logger.info("Removing %s", target)
        shutil.rmtree(target)
    else:
        logger.info("Nothing to clean: %s does not exist", target)


def run_build(variant: str, debug: bool = False) -> Path:
    """Build the variant and return the path to its artefact."""
    spec = VARIANTS[variant]
    target = build_dir(variant, debug)

    command = ["west", "build", "-b", spec["board"], "-d", str(target)]

    if spec["snippet"]:
        command += ["-S", spec["snippet"]]

    cmake_args = list(spec["cmake_args"])

    openocd = TOOLS_ROOT / "openocd-esp32" / "bin" / "openocd.exe"

    if spec.get("needs_openocd_esp32") and openocd.exists():
        scripts = TOOLS_ROOT / "openocd-esp32" / "share" / "openocd" / "scripts"

        # boards/espressif/.../board.cmake resolves OPENOCD relative to
        # ESPRESSIF_TOOLCHAIN_PATH and discards anything outside it, so both
        # have to be given together.
        cmake_args += [
            f"-DESPRESSIF_TOOLCHAIN_PATH={TOOLS_ROOT.as_posix()}",
            f"-DOPENOCD={openocd.as_posix()}",
            f"-DOPENOCD_DEFAULT_PATH={scripts.as_posix()}",
        ]

    if debug:
        # CMakeLists appends prj_local.conf to whatever arrives here, so the
        # local overrides still win over the debug overlay.
        cmake_args.append(f"-DEXTRA_CONF_FILE={PROJECT_ROOT / 'prj_debug.conf'}")

    if cmake_args:
        command += ["--"] + cmake_args

    logger.info("Building %s: %s", variant, " ".join(command))
    subprocess.run(command, cwd=PROJECT_ROOT, check=True)

    artefact = target / "zephyr" / spec["artefact"]

    if not artefact.exists():
        raise FileNotFoundError(f"the build reported success but {artefact} is not there")

    return artefact


def file_artefact(artefact: Path, variant: str, desc: str = None) -> Path:
    """Copy the artefact into output/ under a name that identifies it."""
    OUTPUT_DIR.mkdir(exist_ok=True)

    version = get_firmware_version(desc)
    commit = get_git_commit_hash()
    name = f"concentrator-{variant}_{version}.{commit}{artefact.suffix}"

    destination = OUTPUT_DIR / name
    shutil.copy2(artefact, destination)

    logger.info("Filed %s (%d bytes)", destination, destination.stat().st_size)

    return destination


def main() -> int:
    args = parse_args()
    setup_logger(logger, args.log)
    put_virtualenv_on_path()

    try:
        if args.action in ("clean", "clean_build"):
            run_clean(args.variant, args.debug)

        if args.action == "clean":
            return 0

        check_prerequisites(args.variant)

        artefact = run_build(args.variant, args.debug)

        # A debug build is for a bench, not for a board that ships. Filing it in
        # output/ next to the release artefacts is how one ends up flashed by
        # mistake six weeks later.
        if args.debug:
            logger.info("Debug build at %s, not filed in output/", artefact)
        else:
            file_artefact(artefact, args.variant, args.desc)
    except subprocess.CalledProcessError as error:
        logger.error("Build failed with status %d", error.returncode)
        return error.returncode
    except Exception as error:
        logger.error("%s", error)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
