@echo off
REM Build the release tree and create the Inno Setup installer named by CMake's project version.

setlocal
cd /d "%~dp0..\.."
call "%~dp0..\build\build-release.bat" --target installer
exit /b %errorlevel%
