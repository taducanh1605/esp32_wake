@echo off
setlocal
if "%~1"=="" (
    echo Usage: flash_merged.bat COMx [firmware.bin]
    exit /b 1
)
powershell -ExecutionPolicy Bypass -File "%~dp0flash_merged.ps1" -Port %1 %2
exit /b %ERRORLEVEL%
