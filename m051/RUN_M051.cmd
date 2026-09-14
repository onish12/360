@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 M0.5.1 - proba temporara, numai citire. Inca nu reda sunet.
echo Click dreapta pe acest fisier, Run as administrator.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0runtime\RUN_M051.ps1"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
