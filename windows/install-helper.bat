@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-helper.ps1" %*
set "result=%errorlevel%"
pause
exit /b %result%