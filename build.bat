@echo off
rem Builds Purple Map Fix. Needs Git, xmake and Visual Studio Build Tools already installed.
rem Output is also saved to build-log.txt.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
pause
