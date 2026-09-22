@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
  echo Folosire: CHECK_H15C_LIVE_R2_TRUST.cmd "C:\cale\package"
  exit /b 2
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-H15cLivePackageTrust.ps1" -PackageRoot "%~1"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
