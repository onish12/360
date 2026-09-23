@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15OR - READ-ONLY RECOVERY PROBE / CONDITIONAL INTEL HANDOFF
echo Nu modifica BCD si nu reporneste Windows.
echo Probe-ul nu scrie MMIO sau PCI.
echo H15O este eliminat NUMAI daca PGCTL, CGCTL, GCTL, EM2, PPCTL si starea DSP sunt exact sigure.
echo.
for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-H15orRecovery.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
