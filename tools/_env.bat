@echo off
rem Shared preamble for every script in this folder.
rem
rem Activates the workspace virtualenv if it is not already active, then leaves
rem the caller in the repository root. Sourced with `call`, never run directly.
rem
rem The venv is expected at the WEST WORKSPACE root, one level above this
rem repository - the layout west creates and the one docs/README.md describes.
rem Everything here is relative to this script, so the workspace can live on any
rem drive or path.

if defined VIRTUAL_ENV goto :activated

set "VENV=%~dp0..\..\.venv\Scripts\activate.bat"
if not exist "%VENV%" (
    echo.
    echo ERROR: no virtualenv at %~dp0..\..\.venv
    echo.
    echo Create one at the workspace root and install Zephyr's requirements:
    echo     python -m venv ^<workspace^>\.venv
    echo     ^<workspace^>\.venv\Scripts\python.exe -m pip install --no-cache-dir -r ^<workspace^>\zephyr\scripts\requirements-base.txt
    echo.
    exit /b 1
)
call "%VENV%"

:activated
cd /d "%~dp0.."
exit /b 0
