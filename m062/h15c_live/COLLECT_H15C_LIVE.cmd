@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15C-LIVE - PCI CONFIG READ-ONLY
echo Acest collector NU instaleaza si NU scoate drivere.
echo Ruleaza numai dupa ce filtrul H15C-LIVE a fost instalat prin procedura aprobata.
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-H15cLive.ps1"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
