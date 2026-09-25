@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15J - HDA QUIESCENCE + DSP STATUS / GCTL.CRST 0-1-0 / ROLLBACK
echo Necesita TESTSIGN deja permis si WinRE activ.
echo Nu modifica BCD si nu reporneste Windows.
echo MMIO write: numai HDA GCTL bit0 CRST. CRST revine la 0 numai dupa CORB/RIRB/stream RUN=0.
echo DSP MMIO write, PCI write, DMA, IRQ ownership, firmware si playback: NU.
echo.
for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-H15jTransaction.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
