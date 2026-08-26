# tools

Windows wrappers for building and flashing. Nothing here is part of the
firmware, and nothing here talks to the device once it is running — that lives
in `tests/utils`, following the layout `deepsight-polaris-software` uses.

Each script activates the workspace virtualenv, moves to the repository root and
runs the right `west` invocation, so none of the flags have to be remembered.

```
lora-build.bat [pristine]     wifi-build.bat [pristine]
lora-flash.bat                wifi-flash.bat
                              wifi-monitor.bat
```

`pristine` builds from scratch, which is what a change to `Kconfig`, `prj.conf`
or a devicetree overlay usually needs.

Two things they encapsulate that are easy to get wrong by hand:

**The flash scripts build the build directory path themselves.** `build.dir-fmt`
is `build/{board}/{app}`, and `west flash` has no board to expand it with, so on
its own it looks for a plain `.\build` and gives up.

**`lora-build.bat` passes `-DBOARD_FLASH_RUNNER=jlink`.** The DK has an on-board
J-Link, and the `nrfutil` the default runner wants is shadowed on this machine by
the legacy Python tool of the same name.
