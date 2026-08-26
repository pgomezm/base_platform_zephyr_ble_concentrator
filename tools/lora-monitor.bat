@echo off
rem Open the nRF52840 DK console.
rem
rem The DK enumerates three COM ports and the console is on the lowest numbered
rem one, the J-Link CDC UART. This picks it rather than making you guess.
rem
rem Ctrl-] closes it.

setlocal
call "%~dp0_env.bat" || exit /b 1

python tests\utils\console.py %*
exit /b %ERRORLEVEL%
