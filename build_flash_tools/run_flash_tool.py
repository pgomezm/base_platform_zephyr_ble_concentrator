'''
Flash a concentrator variant.

    python build_flash_tools/run_flash_tool.py --variant lora
    python build_flash_tools/run_flash_tool.py --variant wifi
    python build_flash_tools/run_flash_tool.py --variant lora --file output/concentrator-lora_0.1.0-dev.4f2a91c3.hex

The last form is why output/ exists: putting back exactly what was on a board
last week, without rebuilding it and hoping the result is the same.
'''

import argparse
import logging
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from log_tool import setup_logger  # noqa: E402
from run_build_tool import PROJECT_ROOT, VARIANTS, build_dir  # noqa: E402

logger = logging.getLogger(__name__)


def parse_args():
    parser = argparse.ArgumentParser(description="Flash a concentrator variant.")
    parser.add_argument("--variant", choices=sorted(VARIANTS), default="lora",
                        help="which build to flash (default: lora)")
    parser.add_argument("--debug", action="store_true",
                        help="flash the debug build from build/<variant>-debug")
    parser.add_argument("--file", default=None,
                        help="flash this artefact instead of the build directory's")
    parser.add_argument("--log", choices=["debug", "info", "warning", "error", "critical"],
                        default="info", help="verbosity")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    setup_logger(logger, args.log)

    target = build_dir(args.variant, args.debug)

    if not target.exists():
        logger.error("No build at %s. Build it first with run_build_tool.py --variant %s",
                     target, args.variant)
        return 1

    # The runner shells out to esptool, which lives in the workspace
    # virtualenv. Without it the failure comes from inside west and reads like
    # a missing package rather than a console that never activated it.
    if args.variant == "wifi" and shutil.which("esptool") is None:
        if sys.prefix != sys.base_prefix:
            logger.error("esptool is not on PATH. The active virtualenv is %s; "
                         "install it there with: pip install esptool", sys.prefix)
        else:
            logger.error("esptool is not on PATH, and no virtualenv is active. "
                         "Activate the workspace one and try again.")
        return 1

    # `west flash` and not `west build -t flash`: the CMake flash target is
    # deprecated, and passing the build directory explicitly is what makes
    # `west flash` work at all here - build.dir-fmt keys on the board, and
    # `west flash` has no board to expand it with.
    command = ["west", "flash", "-d", str(target)]

    if args.file:
        artefact = Path(args.file)

        if not artefact.is_absolute():
            artefact = PROJECT_ROOT / artefact

        if not artefact.exists():
            logger.error("No such artefact: %s", artefact)
            return 1

        # The runner still needs a build directory for its own configuration -
        # the chip, the addresses, which tool to invoke. Only the image comes
        # from elsewhere.
        logger.warning("Flashing %s over the build in %s", artefact.name, target)
        command += ["--file", str(artefact)]

    logger.info("%s", " ".join(command))

    try:
        subprocess.run(command, cwd=PROJECT_ROOT, check=True)
    except subprocess.CalledProcessError as error:
        logger.error("Flashing failed with status %d", error.returncode)
        return error.returncode

    if args.variant == "lora":
        logger.info("Start `mosquitto_sub -v -t 'lora/#'` on the gateway before the board "
                    "rejoins, or the join scrolls past unobserved")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
