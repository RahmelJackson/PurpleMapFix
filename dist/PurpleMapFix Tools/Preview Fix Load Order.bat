@echo off
rem Shows what "Fix Load Order.bat" would change. Changes nothing.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Fix Load Order.ps1" -DryRun %*
echo.
pause
