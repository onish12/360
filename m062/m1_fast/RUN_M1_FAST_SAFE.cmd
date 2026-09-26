@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 M1 R8 CANDIDATE SSP-CONTRACT - ONE-SHOT DSP BOOT / AUTOMATIC INTEL ROLLBACK
echo RUNNER_BUILD=m1-fast-safe-20260926-r8-candidate-ssp-contract
echo Fara playback, codec, speaker, BCD sau reboot.
echo Corectie: contract SSP; ETW pe port si offset; cleanup KMDF si rollback Intel automat.
echo.
for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0Run-M1FastSafe.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
