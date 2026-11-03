@echo off
:: Double-click launcher: elevates and runs Make-ClawMacUSB.ps1 next to this file.
setlocal
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Make-ClawMacUSB.ps1" %*
echo.
pause
