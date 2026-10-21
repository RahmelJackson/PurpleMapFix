@echo off
rem Makes the Purple Map Fix map fix permanent in your load order.
rem Close Mod Organizer 2 and the game first. A backup of your plugin list is made.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Fix Load Order.ps1" %*
echo.
pause
