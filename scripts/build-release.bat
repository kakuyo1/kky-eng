@echo off
REM The optimised tree, for running the application at something like its real speed. The
REM development tree stays Debug -- it is the one with the assertions, the TRACE log and the PDB
REM scripts/coverage.sh reads -- so this is a second tree rather than a switch: changing the build
REM type in place rewrites every compile flag and rebuilds the lot anyway.
REM
REM Everything else (finding Qt, entering the MSVC environment, the QML lint) is build.bat's, and
REM the two variables below are the whole difference. RelWithDebInfo and not Release because MSVC's
REM Release flags carry no /Zi, leaving no PDB for a crash stack.
REM
REM Usage: scripts\build-release.bat [extra "cmake --build" arguments]

setlocal
set "LENS_BUILD_PRESET=ninja-qt6-release"
set "LENS_BUILD_DIR=build-ninja-release"
call "%~dp0build.bat" %*
