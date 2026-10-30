@echo off
setlocal
rem Keep batch syntax ASCII so paths work with every Windows console code page.
set "RF_SCRIPT="
for /d %%D in ("%~dp0*") do if exist "%%~fD\build\windows\build.ps1" set "RF_SCRIPT=%%~fD\build\windows\build.ps1"
if not defined RF_SCRIPT (
    echo Cold-Cylinder source tree was not found next to this launcher.
    if not defined COLD_CYLINDER_NO_PAUSE pause
    exit /b 1
)
set "RF_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "RF_POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%RF_POWERSHELL%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%RF_SCRIPT%" %*
set "RF_EXIT=%errorlevel%"
if not defined COLD_CYLINDER_NO_PAUSE pause
exit /b %RF_EXIT%
