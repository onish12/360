@echo off
setlocal
rem Run this copy from the recovery folder on the installed Windows volume.
set "PHASER_WINDOWS=%~d0\Windows"
if not exist "%PHASER_WINDOWS%\System32\config\SYSTEM" (
  echo Windows SYSTEM hive missing on this volume. No change made.
  exit /b 1
)
if exist X:\Windows\System32\wpeutil.exe goto offline
echo Run this file only from the Windows Recovery Command Prompt.
exit /b 1
:offline
reg load HKLM\PHASER360_M051_RECOVERY "%PHASER_WINDOWS%\System32\config\SYSTEM"
if errorlevel 1 exit /b 1
set "PHASER_FAILED=0"
for /f "tokens=*" %%K in ('reg query HKLM\PHASER360_M051_RECOVERY ^| findstr /r /e "ControlSet[0-9][0-9][0-9]"') do (
  reg query "%%K\Services\phaser360_m051_mmio_ro" >nul 2>&1
  if not errorlevel 1 (
    reg add "%%K\Services\phaser360_m051_mmio_ro" /v Start /t REG_DWORD /d 4 /f
    if errorlevel 1 set "PHASER_FAILED=1"
  )
)
reg unload HKLM\PHASER360_M051_RECOVERY
if errorlevel 1 exit /b 1
if "%PHASER_FAILED%"=="1" exit /b 1
echo Experimental M0.5.1 service disabled where present. No Intel driver restored.
echo Start Windows, then use runtime\CLEANUP_M051.ps1 from the same recovery folder.
exit /b 0
