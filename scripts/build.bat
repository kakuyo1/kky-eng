@echo off
REM Configure and build Lens with Ninja.
REM
REM Ninja cannot locate MSVC by itself, so this script enters the Visual Studio environment
REM first and only then drives CMake. Configure runs only when the build directory is
REM missing; every later call is a pure incremental ninja build.
REM
REM Usage: scripts\build.bat [extra "cmake --build" arguments]
REM        scripts\build.bat --target lens_gtest_unit

setlocal
cd /d "%~dp0.."

REM vcvarsall.bat shells out to vswhere.exe and expects it on PATH. Add the Installer
REM directory up front, otherwise every build prints a stray "vswhere.exe is not
REM recognized" even though the environment is set up correctly.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"

REM Locate Visual Studio through a temp file rather than "for /f": a command line that
REM carries "Program Files (x86)" trips cmd's parenthesis parser.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIRFILE=%TEMP%\lens_vsdir.txt"
"%VSWHERE%" -latest -property installationPath > "%VSDIRFILE%" 2>nul
set /p VSDIR=<"%VSDIRFILE%"
del "%VSDIRFILE%" >nul 2>&1

if not defined VSDIR (
  echo [build] Visual Studio not found. Install the C++ workload, or call
  echo [build] cmake --preset ninja-qt6 from a VS developer command prompt instead.
  exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [build] could not enter the MSVC environment at "%VSDIR%"
  exit /b 1
)

if not exist "build-ninja\build.ninja" (
  cmake --preset ninja-qt6
  if errorlevel 1 exit /b 1
)

cmake --build --preset ninja-qt6 %*
