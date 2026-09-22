@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15C-LIVE R2 - READ-ONLY INSTALL PREFLIGHT
echo Nu instaleaza driver, nu modifica trust, BCD, registry sau device state.
echo.
if "%~1"=="" (
  echo Folosire: RUN_H15C_LIVE_PREFLIGHT.cmd "C:\cale\package"
  exit /b 2
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-H15cLiveInstallPreflight.ps1" -PackageRoot "%~1"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
