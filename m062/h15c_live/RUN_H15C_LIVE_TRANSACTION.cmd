@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 H15C-LIVE R2 - SIGNED INSTALL / CAPTURE / ROLLBACK
echo Necesita package-ul R2 semnat si TESTSIGN deja permis.
echo Adauga temporar certificatul exact in LocalMachine Root + TrustedPublisher.
echo Dupa rollback complet elimina certificatul exact. Nu modifica BCD si nu reporneste Windows.
echo.
if "%~1"=="" (
  echo Folosire: RUN_H15C_LIVE_TRANSACTION.cmd "C:\cale\package"
  exit /b 2
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-H15cLiveTransaction.ps1" -PackageRoot "%~1"
set "PHASER_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_EXIT%
pause
exit /b %PHASER_EXIT%
