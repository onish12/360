@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H13 - baseline Intel driver export only.
echo No install, uninstall, device restart, MMIO, DSP boot or playback.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Backup-BaselineDriver.ps1"
set "RC=%ERRORLEVEL%"
echo.
echo Cod iesire: %RC%
pause
exit /b %RC%
