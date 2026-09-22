@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15C-LIVE R2 - SIGNED INSTALL / CAPTURE / ROLLBACK
echo Necesita package-ul R2 semnat si TESTSIGN deja permis.
echo Adauga temporar certificatul exact in LocalMachine Root + TrustedPublisher.
echo Dupa rollback complet elimina certificatul exact. Nu modifica BCD si nu reporneste Windows.
echo.
set "PHASER_PACKAGE=%~1"
if not defined PHASER_PACKAGE set "PHASER_PACKAGE=%~dp0"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-H15cLiveTransaction.ps1" -PackageRoot "%PHASER_PACKAGE%"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
