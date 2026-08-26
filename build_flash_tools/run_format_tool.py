'''
Apply .clang-format to the C++ sources.

    python build_flash_tools/run_format_tool.py            # rewrite the files
    python build_flash_tools/run_format_tool.py --check    # report, change nothing

The configuration is .clang-format at the repository root, taken verbatim from
deepsight-polaris-software so both repositories look the same: 4-space indent,
100 columns, braces on their own line.

`SortIncludes: Never` is the setting worth knowing about. Include order in this
firmware is deliberate - module headers, then project headers, then the
platform's - and a formatter that sorted them alphabetically would destroy that
without anyone noticing.

clang-format is not part of the Zephyr SDK. Install it into the workspace
virtualenv:

    pip install --no-cache-dir clang-format
'''

import argparse
import logging
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from log_tool import setup_logger  # noqa: E402

logger = logging.getLogger(__name__)

PROJECT_ROOT = Path(__file__).resolve().parents[1]

#: Only the firmware. tests/ is Python and build/ is generated.
SOURCE_DIRS = ["src"]
SUFFIXES = {".cpp", ".hpp", ".c", ".h"}


def parse_args():
    parser = argparse.ArgumentParser(description="Format the C++ sources.")
    parser.add_argument("--check", action="store_true",
                        help="list files that would change and exit non-zero, writing nothing")
    parser.add_argument("--log", choices=["debug", "info", "warning", "error", "critical"],
                        default="info", help="verbosity")
    return parser.parse_args()


def find_clang_format() -> str:
    for name in ("clang-format", "clang-format.exe"):
        found = shutil.which(name)
        if found:
            return found

    logger.error("clang-format is not on PATH. Install it into the workspace virtualenv:")
    logger.error("    pip install --no-cache-dir clang-format")
    return ""


def collect_sources():
    files = []

    for directory in SOURCE_DIRS:
        root = PROJECT_ROOT / directory

        if not root.exists():
            continue

        for path in sorted(root.rglob("*")):
            if path.is_file() and path.suffix in SUFFIXES:
                files.append(path)

    return files


def main() -> int:
    args = parse_args()
    setup_logger(logger, args.log)

    binary = find_clang_format()
    if not binary:
        return 1

    files = collect_sources()
    if not files:
        logger.error("No sources found under %s", ", ".join(SOURCE_DIRS))
        return 1

    logger.info("%s over %d file(s)", "Checking" if args.check else "Formatting", len(files))

    if args.check:
        # --dry-run --Werror makes clang-format report and fail rather than
        # rewrite, which is what a CI step or a pre-commit hook wants.
        command = [binary, "--dry-run", "--Werror"] + [str(f) for f in files]
    else:
        command = [binary, "-i"] + [str(f) for f in files]

    result = subprocess.run(command, cwd=PROJECT_ROOT)

    if result.returncode != 0:
        if args.check:
            logger.warning("Some files are not formatted. Run without --check to fix them.")
        else:
            logger.error("clang-format failed with status %d", result.returncode)
        return result.returncode

    logger.info("Done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
