@echo off
rem Flash the LoRaWAN variant to the nRF52840 DK.
rem
rem Start `mosquitto_sub -v -t 'lora/#'` on the gateway BEFORE running this. The
rem flash resets the board, the join goes out immediately, and the next join is
rem not identical - the DevNonce has moved.

setlocal
call "%~dp0_env.bat" || exit /b 1

rem build.dir-fmt is build/{board}/{app}, and `west flash` has no board to
rem expand it with, so the path is built here instead. The app name is the
rem repository folder, which _env.bat has already made the current directory.
for %%I in ("%CD%") do set "APP=%%~nxI"

west flash -d "build\nrf52840dk\nrf52840\%APP%"
exit /b %ERRORLEVEL%
