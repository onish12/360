@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 M0.6.15F - captura IRQ exclusiv READ-ONLY.
echo Nu instaleaza drivere, nu reporneste dispozitivul, nu face MMIO si nu reda sunet.
echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-IrqEvidence.ps1"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
