@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Scripts\StartPlayFabEditor.ps1"
if errorlevel 1 (
    echo.
    echo Launch failed. Read the message above.
    pause
    exit /b 1
)
exit /b 0
