@echo off
rem Build the Wi-Fi variant: ESP32-S3-DevKitC-1.
rem
rem No backend flag: boards/esp32s3_devkitc_esp32s3_procpu.conf sets
rem CONFIG_APP_LINK_WIFI=y, because that board has neither an SX127x nor a wired
rem interface and building it any other way is a mistake rather than a choice.
rem
rem Needs hal_espressif and its binary blobs. If Kconfig reports WIFI_ESP32
rem unavailable, that is what is missing:
rem     west update
rem     west blobs fetch hal_espressif
rem
rem   wifi-build.bat            incremental
rem   wifi-build.bat pristine   from scratch

setlocal
call "%~dp0_env.bat" || exit /b 1

set "PRISTINE="
if /i "%~1"=="pristine" set "PRISTINE=--pristine"

west build -b esp32s3_devkitc/esp32s3/procpu %PRISTINE%
exit /b %ERRORLEVEL%
