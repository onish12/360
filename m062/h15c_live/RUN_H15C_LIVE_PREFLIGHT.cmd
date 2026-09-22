@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15C-LIVE R2 - READ-ONLY INSTALL PREFLIGHT
echo Nu instaleaza driver, nu modifica trust, BCD, registry sau device state.
echo.
set "PHASER_PACKAGE=%~1"
if not defined PHASER_PACKAGE set "PHASER_PACKAGE=%~dp0"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-H15cLiveInstallPreflight.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
