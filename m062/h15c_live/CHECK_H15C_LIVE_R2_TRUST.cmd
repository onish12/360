@echo off
setlocal
cd /d "%~dp0"
set "PHASER_PACKAGE=%~1"
if not defined PHASER_PACKAGE (
  for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
) else (
  for %%I in ("%PHASER_PACKAGE%") do set "PHASER_PACKAGE=%%~fI"
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-H15cLivePackageTrust.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
