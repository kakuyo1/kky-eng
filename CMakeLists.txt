cmake_minimum_required(VERSION 3.21)
project(Lens VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt meta-object compiler, needed by the QObject classes in lens_llm / lens_app.
set(CMAKE_AUTOMOC ON)

# Test and QuickTest are for test/qtest/ alone: a QTest run needs a QGuiApplication and a Qt
# Quick scene, which is what that directory exists to get.
# Machine paths come from config/paths.json. A literal in a CMakeLists is a value that is wrong on
# the next machine, and duplicated by the time it is written a third time. config/README.md owns
# the rule and its exemptions, docs/adr/0006 the reasoning.
set(LENS_CONFIG_DIR "${CMAKE_SOURCE_DIR}/config")
if(NOT EXISTS "${LENS_CONFIG_DIR}/paths.json")
  message(FATAL_ERROR "config/paths.json is missing; see config/README.md")
endif()
file(READ "${LENS_CONFIG_DIR}/paths.json" LENS_PATHS_RAW)
if(EXISTS "${LENS_CONFIG_DIR}/paths.local.json")
  file(READ "${LENS_CONFIG_DIR}/paths.local.json" LENS_PATHS_LOCAL_RAW)
else()
  set(LENS_PATHS_LOCAL_RAW "{}")
endif()

# One key out of the two files, the local one winning -- the same merge the PowerShell reader
# does. string(JSON) leaves its error variable at NOTFOUND when the read succeeded, and NOTFOUND
# is a false constant in if(), so "if(error)" reads as "it failed".
function(lens_path out key)
  string(JSON value ERROR_VARIABLE error GET "${LENS_PATHS_RAW}" "${key}")
  if(error)
    message(FATAL_ERROR "config/paths.json has no '${key}'; see config/README.md")
  endif()
  string(JSON local ERROR_VARIABLE error GET "${LENS_PATHS_LOCAL_RAW}" "${key}")
  if(NOT error)
    set(value "${local}")
  endif()
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

# The one consumer CMake has so far; setup.cpp gets it as a compile definition rather than
# reading the file at run time.
lens_path(LENS_SYSTEM_FONTS systemFonts)

# A preset cannot read config/paths.json, so CMakePresets.json takes the Qt prefix from the
# environment and scripts/build.bat exports it from the file. Saying so here beats Qt's own
# "package configuration file not found", which does not name the lever.
if((NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "") AND NOT DEFINED ENV{QT_ROOT})
  message(FATAL_ERROR
    "Qt was not located. Set QT_ROOT to the Qt 6 prefix -- config/paths.json carries it as"
    " qtRoot -- or configure with -DCMAKE_PREFIX_PATH=<prefix>. scripts/build.bat does this"
    " for you; see config/README.md.")
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network Gui Quick Widgets Svg LinguistTools Test QuickTest)

# Qt's own defaults for QML modules: resources under :/qt/qml/, and a qmldir for every extra
# directory holding .qml files. Setting both here keeps the configure step quiet.
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

if(MSVC)
  # Sources carry UTF-8 bytes. MSVC would otherwise decode them as code page 936 and
  # corrupt the tokens, so force UTF-8 for source decoding as well as literals.
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

# Dev only: require a consent dialog before any request leaves for the LLM. OFF for release.
# Public, because the gate lives at the call site in AppController: the macro decides whether
# that branch is compiled in at all, so the flag has to reach whoever includes the header.
option(DEV_SEND_CONFIRM "Dev: force consent before sending to LLM" OFF)

# Compile the lens_core measurement points in (core/profile.h). Off by default: it changes
# how lens_core is built, so turning it on means a rebuild of that target, not a rerun.
option(LENS_ENABLE_PROFILE "Compile lens_core profile instrumentation in" OFF)

# QML profiler builds need the engine's debugging hooks. Keep this off for normal builds: it
# changes the executable's instrumentation and is only useful for a dedicated measurement tree.
option(LENS_ENABLE_QML_DEBUG "Compile the lens executable with QML debugging enabled" OFF)

# Vendored dependencies. spdlog builds its own static library; see third_party/spdlog.
add_subdirectory(third_party/spdlog)

# gmock and the install rules are unused parts of the distribution; see third_party/googletest.
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/googletest)

add_subdirectory(src/core)
add_subdirectory(src/llm)
add_subdirectory(src/app)

if(LENS_ENABLE_QML_DEBUG)
  target_compile_definitions(lens PRIVATE QT_QML_DEBUG)
endif()

# Defined here rather than inside src/app so the value is the same everywhere, and so a
# release build simply does not contain the consent dialog's code.
if(DEV_SEND_CONFIRM)
  target_compile_definitions(lens_app PUBLIC DEV_SEND_CONFIRM)
endif()

# Self-check target: links lens_core + lens_llm, built on the host only, never shipped.
add_subdirectory(test)
