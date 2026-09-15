@echo off
setlocal
cd /d "%~dp0"
echo PHASER360 AUDIO AUTO - analiza si actiune dupa starea detectata.
echo Ruleaza acest fisier ca administrator. Asteapta arhiva RESULT_AUDIO.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0runtime\RUN_AUDIO.ps1"
set "PHASER_AUDIO_EXIT=%ERRORLEVEL%"
echo.
echo Cod iesire: %PHASER_AUDIO_EXIT%  (0=proba reusita, 3=pregatire terminata, 2=recuperare necesara, 1=oprire)
pause
exit /b %PHASER_AUDIO_EXIT%
