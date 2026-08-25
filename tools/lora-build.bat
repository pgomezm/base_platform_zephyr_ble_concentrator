@echo off
rem Build the LoRaWAN variant: nRF52840 DK with an inAir9 (SX1276).
rem
rem This is the Kconfig default, so no backend flag is needed. The flash runner
rem is pinned to jlink because the DK has an on-board J-Link and because the
rem nrfutil the default runner wants is shadowed on this machine by the legacy
rem Python tool of the same name.
rem
rem   lora-build.bat            incremental
rem   lora-build.bat pristine   from scratch, after changing Kconfig or the overlay

setlocal
call "%~dp0_env.bat" || exit /b 1

set "PRISTINE="
if /i "%~1"=="pristine" set "PRISTINE=--pristine"

west build -b nrf52840dk/nrf52840 %PRISTINE% -- -DBOARD_FLASH_RUNNER=jlink
exit /b %ERRORLEVEL%
