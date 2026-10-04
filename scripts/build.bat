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

REM The Qt prefix comes from config/paths.json, through the same reader the PowerShell scripts use.
REM CMakePresets.json can only read the environment -- a preset cannot open the file -- so it takes
REM QT_ROOT and this exports it. config/README.md owns the rule, docs/adr/0006 the reasoning.
REM Through a temp file rather than "for /f", for the same reason the VSDIR lookup below avoids it:
REM a command line carrying quotes or parens trips cmd's own parser. This one has both.
set "QTFILE=%TEMP%\lens_qtroot.txt"
powershell -NoProfile -ExecutionPolicy Bypass -Command ". .\scripts\paths.ps1; $p = Get-LensPaths -Root $PWD; $p.qtRoot" > "%QTFILE%" 2>nul
set /p QT_ROOT=<"%QTFILE%"
del "%QTFILE%" >nul 2>&1
if not defined QT_ROOT (
  echo [build] config\paths.json has no readable qtRoot -- see config\README.md
  exit /b 1
)
REM cmd's "if exist" reads a forward slash as a switch, and the config writes paths with forward
REM slashes, so the value is turned back into backslashes before it is tested. Both echo texts
REM below avoid parentheses for the reason the VSDIR note gives: inside an if block, a stray
REM "(" opens a group cmd never closes, and the sentence quietly becomes the block.
set "QTBACK=%QT_ROOT:/=\%"
if not exist "%QTBACK%\lib\cmake\Qt6" (
  echo [build] Qt is not at "%QT_ROOT%" -- edit config\paths.json, see config\README.md
  exit /b 1
)

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
if errorlevel 1 exit /b 1

REM The QML lint needs the response file CMake writes when it configures the tree, and that tree
REM is this script's doing, so it runs here rather than in the pre-commit hook. A machine with no
REM sh on PATH (a bare cmd session) skips it instead of failing the build, the same contract the
REM hook keeps when one of its tools is missing. Its exit code is this script's.
where sh >nul 2>&1
if errorlevel 1 (
  echo [build] sh is not on PATH, skipping the QML lint
) else (
  sh scripts/qml-lint.sh
)
