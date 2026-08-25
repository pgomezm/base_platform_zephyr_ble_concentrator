@echo off
rem Open the ESP32-S3 console.
rem
rem The console is on the native USB-Serial-JTAG port, not a UART, and Windows
rem gives it a different COM number depending on which socket it is plugged
rem into. This finds it rather than making you look.
rem
rem Ctrl-] closes it.

setlocal
call "%~dp0_env.bat" || exit /b 1

west espressif monitor -d build/esp32s3_devkitc/esp32s3/procpu/base_platform_zephyr_ble_concentrator
exit /b %ERRORLEVEL%
