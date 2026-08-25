@echo off
rem Flash the Wi-Fi variant to the ESP32-S3-DevKitC-1.
rem
rem No debug probe involved: the S3 has native USB-Serial-JTAG, so esptool talks
rem to the chip over the same USB cable that powers it.

setlocal
call "%~dp0_env.bat" || exit /b 1

west build -b esp32s3_devkitc/esp32s3/procpu -t flash
exit /b %ERRORLEVEL%
