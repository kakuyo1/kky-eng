cmake_minimum_required(VERSION 3.21)
project(Lens VERSION 1.0.0 LANGUAGES CXX)

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
# environment and scripts/build/build.bat exports it from the file. Saying so here beats Qt's own
# "package configuration file not found", which does not name the lever.
if((NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "") AND NOT DEFINED ENV{QT_ROOT})
  message(FATAL_ERROR
    "Qt was not located. Set QT_ROOT to the Qt 6 prefix -- config/paths.json carries it as"
    " qtRoot -- or configure with -DCMAKE_PREFIX_PATH=<prefix>. scripts/build/build.bat does this"
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

add_subdirectory(src/util)
add_subdirectory(src/core)
add_subdirectory(src/llm)
add_subdirectory(src/app)

if(LENS_ENABLE_QML_DEBUG)
  target_compile_definitions(lens PRIVATE QT_QML_DEBUG)
endif()

# Self-check target: links lens_core + lens_llm, built on the host only, never shipped.
add_subdirectory(test)

# Precompiled headers, applied here so the whole decision is one list rather than a line in every
# CMakeLists. A target only earns one by having enough translation units to pay for its own copy
# of the header -- the QML ahead-of-time files give lens_app 25 of them, and what it is worth is in
# cmake/lens_pch.h. lens_core and lens_gtest_perf link no Qt and cannot take it. The Quick-level
# half is a second file because it does not resolve for a Core-only target.
foreach(LENS_PCH_TARGET lens_app lens_llm lens
    lens_gtest_unit lens_gtest_smoke lens_gtest_integration
    lens_qtest_components lens_qtest_surfaces)
  target_precompile_headers(${LENS_PCH_TARGET} PRIVATE "${CMAKE_SOURCE_DIR}/cmake/lens_pch.h")
endforeach()
foreach(LENS_PCH_TARGET lens_app lens
    lens_gtest_integration lens_qtest_components lens_qtest_surfaces)
  target_precompile_headers(${LENS_PCH_TARGET} PRIVATE "${CMAKE_SOURCE_DIR}/cmake/lens_pch_quick.h")
endforeach()

# Packaging. The version the installer reads comes from project() above, through the three-line
# include installer/lens.iss opens, so there is one number to bump and not two.
configure_file(
  "${CMAKE_SOURCE_DIR}/installer/version.iss.in"
  "${CMAKE_BINARY_DIR}/installer/version.iss"
  @ONLY
)

# Two steps, driven by naming the target: windeployqt writes the Qt runtime into
# installer/payload beside the executable, then Inno Setup (installer/lens.iss) packs that
# payload and data/ into one installer under the build tree's installer/.
#
# The payload is wiped first. A dependency dropped from the deploy options would otherwise go on
# sitting there from the previous run, and the size would be measuring a directory nothing
# produces any more.
#
# Run it in the release tree: scripts\build\build-release.bat --target installer. The target is defined
# in every tree, but installer/lens.iss names build-ninja-release as what it packs, so from
# another tree it would deploy the payload here and then pack whatever is over there.
#
# Two machine paths, both from config/paths.json; config/README.md owns the rule.
lens_path(LENS_QT_BIN qtBin)
lens_path(LENS_INNO_SETUP innoSetup)

set(LENS_PAYLOAD_DIR "${CMAKE_BINARY_DIR}/installer/payload")

add_custom_target(installer
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${LENS_PAYLOAD_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${LENS_PAYLOAD_DIR}"
  COMMAND "${LENS_QT_BIN}/windeployqt.exe" --release --dir "${LENS_PAYLOAD_DIR}"
          --qmldir "${CMAKE_SOURCE_DIR}/src/app/qml"
          --no-compiler-runtime
          --no-opengl-sw
          --no-translations
          --skip-plugin-types qmltooling
          "$<TARGET_FILE:lens>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:lens>" "${LENS_PAYLOAD_DIR}"
  COMMAND "${LENS_INNO_SETUP}" "/I${CMAKE_BINARY_DIR}/installer" "${CMAKE_SOURCE_DIR}/installer/lens.iss"
  DEPENDS lens
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Packaging Lens: windeployqt, then Inno Setup"
  VERBATIM
)
