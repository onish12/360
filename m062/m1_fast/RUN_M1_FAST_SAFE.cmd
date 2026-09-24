@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 M1 R5 CAP-CHAIN FIX - ONE-SHOT DSP BOOT / AUTOMATIC INTEL ROLLBACK
echo RUNNER_BUILD=m1-fast-safe-20260924-r5-cap-chain-fix
echo Fara playback, codec, speaker, BCD sau reboot.
echo Corectie: HDA capability chain validata in BAR; ETW pre-bind; rollback Intel automat.
echo.
for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0Run-M1FastSafe.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
