@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15O - STAGED PRE-FW POWER POLICY / EXACT ROLLBACK
echo Necesita TESTSIGN deja permis si WinRE activ.
echo Nu modifica BCD si nu reporneste Windows.
echo Etape: GPROCEN ON, CGCTL bit1 OFF, EM2.L1SEN OFF, PGCTL bit2 ON; snapshot dupa fiecare.
echo DSP MMIO write, DMA, IRQ ownership, firmware, DSP boot si playback: NU.
echo.
for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-H15oTransaction.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
