@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15M - HDA PP CAPABILITY DISCOVERY / GCTL EXACT ROLLBACK
echo Necesita TESTSIGN deja permis si WinRE activ.
echo Nu modifica BCD si nu reporneste Windows.
echo MMIO write: numai HDA GCTL bit0 CRST, 0-1-0, deja validat fizic.
echo PPCTL/PPSTS, DSP si PCI: doar citire. EM2 write, DMA, IRQ ownership, firmware si playback: NU.
echo.
for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-H15mTransaction.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
