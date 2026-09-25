@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15D-LIVE R1 - BOUNDED PCI WRITE / READBACK / RESTORE / ROLLBACK
echo Necesita TESTSIGN deja permis si WinRE activ.
echo Nu modifica BCD si nu reporneste Windows.
echo Nu face MMIO, DMA, DSP boot sau playback.
echo.
set "PHASER_PACKAGE=%~1"
if not defined PHASER_PACKAGE (
  for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
) else (
  for %%I in ("%PHASER_PACKAGE%") do set "PHASER_PACKAGE=%%~fI"
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-H15dLiveR1.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
