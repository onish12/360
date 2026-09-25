@echo off
setlocal
rem Run this copy as administrator from the recorded recovery directory.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0runtime\CLEANUP_M051.ps1"
echo Cod iesire: %errorlevel%
pause
