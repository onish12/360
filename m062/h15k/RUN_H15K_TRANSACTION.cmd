@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15K - ADSPCS CSTALL+CRST CORES0/1 / EXACT ROLLBACK
echo Necesita TESTSIGN deja permis si WinRE activ.
echo Nu modifica BCD si nu reporneste Windows.
echo MMIO write: numai DSP ADSPCS CSTALL[1:0] si CRST[1:0], apoi rollback exact.
echo SPA/CPA write, HDA MMIO write, PCI write, DMA, IRQ ownership, firmware si playback: NU.
echo.
for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-H15kTransaction.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
