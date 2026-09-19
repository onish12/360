@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0runtime\REPAIR_AUDIO.ps1"
echo Cod iesire: %errorlevel%
pause
