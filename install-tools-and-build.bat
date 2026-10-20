@echo off
rem Installs any missing build tools with winget (Git, xmake, Visual Studio 2022
rem Build Tools with C++), then builds Purple Map Fix.
rem Windows may ask for administrator permission while installing.
rem Output is also saved to build-log.txt.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -InstallMissing
pause
