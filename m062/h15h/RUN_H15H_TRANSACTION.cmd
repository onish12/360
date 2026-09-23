@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15H - FUNCTION DRIVER PCI POLICY / READ-ONLY MMIO / ROLLBACK
echo Necesita TESTSIGN deja permis si WinRE activ.
echo Nu modifica BCD si nu reporneste Windows.
echo PCI write: numai 0x44 bit2 si 0x48 bit1, cu restore exact.
echo MMIO write, DMA, IRQ ownership, firmware si playback: NU.
echo.
for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-H15hTransaction.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
