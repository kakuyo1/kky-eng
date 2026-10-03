cmake_minimum_required(VERSION 3.21)
project(Lens VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt meta-object compiler, needed by the QObject classes in lens_llm / lens_app.
set(CMAKE_AUTOMOC ON)

# Test and QuickTest are for test/qtest/ alone: a QTest run needs a QGuiApplication and a Qt
# Quick scene, which is what that directory exists to get.
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

# Vendored dependencies. spdlog builds its own static library; see third_party/spdlog.
add_subdirectory(third_party/spdlog)

# gmock and the install rules are unused parts of the distribution; see third_party/googletest.
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/googletest)

add_subdirectory(src/core)
add_subdirectory(src/llm)
add_subdirectory(src/app)

# Defined here rather than inside src/app so the value is the same everywhere, and so a
# release build simply does not contain the consent dialog's code.
if(DEV_SEND_CONFIRM)
  target_compile_definitions(lens_app PUBLIC DEV_SEND_CONFIRM)
endif()

# Self-check target: links lens_core + lens_llm, built on the host only, never shipped.
add_subdirectory(test)
