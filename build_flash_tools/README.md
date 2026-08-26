# build_flash_tools

Python tools for building and flashing, in the shape
`deepsight-polaris-software` uses: the version comes out of `src/version.h`, the
commit hash out of git, and the result is filed in `output/` under a name that
says what it is.

```sh
python build_flash_tools/run_build_tool.py --variant lora
python build_flash_tools/run_build_tool.py --variant wifi --action clean_build
python build_flash_tools/run_flash_tool.py --variant wifi
```

Variants are `lora`, `tcp` and `wifi`. Each builds into `build/<variant>/`, one
directory apiece — the LoRa and TCP builds share a board and would otherwise
overwrite each other, and they do not even share a devicetree, since the TCP
snippet deletes the SX127x node the board overlay declares.

## Why the copy into output/

A build directory holds exactly one `zephyr.hex`, and the next build overwrites
it. Weeks later, "what is actually on that board" has no answer.

```
output/concentrator-lora_0.1.0-dev.4f2a91c3.hex
```

answers it, and `run_flash_tool.py --file` puts it back without rebuilding and
hoping the result is identical.

The binaries are gitignored. The directory is kept by a `.gitkeep` so the tools
have somewhere to write on a fresh clone; a repository is not an artefact store.

## The dirty marker

A tree with uncommitted changes produces `…4f2a91c3-dirty.hex` and a warning.
Polaris does not do this and it is the one place these tools deliberately
differ: a binary built from uncommitted work but labelled with a clean commit
hash is a file that lies about its own contents, and it lies exactly when it
matters — when something is wrong and the hash is what you are trusting.

## The console

Not here: `python tests/utils/console.py`. It finds the board's serial port by
USB vendor id and attaches, and it lives with the other things that talk to a
running device rather than with the things that build one.

```sh
python tests/utils/console.py            # find the board and attach
python tests/utils/console.py --list     # show what is connected
```

## Formatting

```sh
python build_flash_tools/run_format_tool.py --check    # report, change nothing
python build_flash_tools/run_format_tool.py            # rewrite
```

clang-format is not part of the Zephyr SDK: `pip install --no-cache-dir clang-format`.
