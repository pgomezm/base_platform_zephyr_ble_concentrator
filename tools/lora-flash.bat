@echo off
rem Flash the LoRaWAN variant to the nRF52840 DK.
rem
rem Uses `west build -t flash` rather than `west flash`. They do the same thing,
rem but only this one can work out the build directory on its own: build.dir-fmt
rem is build/{board}/{app}, and `west flash` has no board to expand it with, so
rem it looks for a plain .\build and gives up.
rem
rem Start `mosquitto_sub -v -t 'lora/#'` on the gateway BEFORE running this. The
rem flash resets the board, the join goes out immediately, and the next join is
rem not identical - the DevNonce has moved.

setlocal
call "%~dp0_env.bat" || exit /b 1

west build -b nrf52840dk/nrf52840 -t flash
exit /b %ERRORLEVEL%
