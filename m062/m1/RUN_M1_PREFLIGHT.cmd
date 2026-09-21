@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 M0.6.15H12 - M1 preflight READ-ONLY.
echo Nu instaleaza sau schimba driverul, nu reporneste dispozitivul, nu face MMIO si nu reda sunet.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-M1Preflight.ps1"
set EC=%ERRORLEVEL%
echo.
echo Cod iesire: %EC%
pause
exit /b %EC%
