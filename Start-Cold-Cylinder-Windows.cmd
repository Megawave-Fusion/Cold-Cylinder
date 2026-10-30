@echo off
setlocal
rem Works inside the extracted Windows package and beside the source tree.
set "RF_EXE="
if exist "%~dp0Cold-Cylinder.exe" set "RF_EXE=%~dp0Cold-Cylinder.exe"
if not defined RF_EXE for /d %%D in ("%~dp0*") do if exist "%%~fD\Windows-x64\Cold-Cylinder.exe" set "RF_EXE=%%~fD\Windows-x64\Cold-Cylinder.exe"
if not defined RF_EXE (
    echo Cold-Cylinder.exe was not found. Extract the complete Windows ZIP, or run Build-Cold-Cylinder-Windows.cmd in the source tree.
    if not defined COLD_CYLINDER_NO_PAUSE pause
    exit /b 1
)
start "Cold-Cylinder" "%RF_EXE%" %*
exit /b %errorlevel%
