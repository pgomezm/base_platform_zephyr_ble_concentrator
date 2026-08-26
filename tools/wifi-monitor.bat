@echo off
rem Open the ESP32-S3 console.
rem
rem Zephyr's console is uart0 (zephyr,console in the board's dts), which on the
rem DevKitC-1 goes to the CP210x bridge - the socket labelled UART, not the one
rem labelled USB. The USB socket is the S3's native USB-Serial-JTAG and carries
rem the ROM and bootloader console, which is a different thing with a nearly
rem identical Kconfig name: ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED is the HAL's,
rem ESP_CONSOLE_UART is the one this build uses.
rem
rem Windows numbers the port differently per socket, so this finds it.
rem
rem Not `west espressif monitor`: that wants a build directory and disagrees
rem with this repository's build.dir-fmt about where one is.
rem
rem Ctrl-] closes it.

setlocal
call "%~dp0_env.bat" || exit /b 1

python tests\utils\console.py %*
exit /b %ERRORLEVEL%
