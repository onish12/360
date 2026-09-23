@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15OR R2 - NONINTERACTIVE READ-ONLY RECOVERY / CONDITIONAL INTEL HANDOFF
echo RUNNER_BUILD=h15or-r2.2-package-contract-fix-20260923
echo Nu modifica BCD si nu reporneste Windows.
echo Acest pachet NU foloseste Set-Content si verifica SHA256-ul propriului runner.
echo.
for %%I in ("%~dp0.") do set "PHASER_PACKAGE=%%~fI"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0Run-H15orR2Recovery.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
