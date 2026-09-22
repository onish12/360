@echo off
setlocal
cd /d "%~dp0"
set "PHASER_PACKAGE=%~1"
if not defined PHASER_PACKAGE set "PHASER_PACKAGE=%~dp0"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-H15cLivePackageTrust.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
