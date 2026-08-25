@echo off
rem Flash the Wi-Fi variant to the ESP32-S3-DevKitC-1.
rem
rem No debug probe involved: the S3 has native USB-Serial-JTAG, so esptool talks
rem to the chip over the same USB cable that powers it.

setlocal
call "%~dp0_env.bat" || exit /b 1

rem build.dir-fmt is build/{board}/{app}, and `west flash` has no board to
rem expand it with, so the path is built here instead. The app name is the
rem repository folder, which _env.bat has already made the current directory.
for %%I in ("%CD%") do set "APP=%%~nxI"

west flash -d "build\esp32s3_devkitc\esp32s3\procpu\%APP%"
exit /b %ERRORLEVEL%
