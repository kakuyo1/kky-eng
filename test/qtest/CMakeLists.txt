# The Qt side of the tests: the components and the surfaces, driven through a real QML engine.
# gtest covers everything up to and including Qt Core; everything that needs a scene lives here.
#
# A case is a .qml file loaded from the source tree at run time, not a type in a QML module.
# Nothing adds it to anyone's QML_FILES, so qmllint never reads it and the ratchet in
# scripts/qml-lint.sh is untouched by a new case. It does still `import Lens`, though, which is
# why every target here links the plugin -- a static plugin is only imported when its init
# object is linked in, and without it every component comes back as "not a type" rather than as
# a failure. src/app/CMakeLists.txt and docs/adr/0004 carry the long version.
#
# The cases are run from the source directory rather than a copy of it, through the directory
# each target bakes in below. They are inputs, not build products.
#
# main.cpp is shared: the suite's name and its case directory are compile definitions, so a
# target is one add_executable. setup.cpp is shared too -- LENS_QTEST_SINGLETONS is what makes
# one of the two actually build the C++ singletons, and it is the only difference between them.

# The components: no C++ singleton is touched at creation, so this one runs headless with
# nothing on PATH but Qt's own DLLs. QT_QPA_PLATFORM=offscreen is the whole headless story;
# see TEST.md section 2 for the command.
# Like the gtest targets, these are out of the default build: `all` is the application alone, and
# a suite is built by naming it (`scripts/build.bat --target lens_qtest_components`). They change
# only when the QML or the two C++ types the surfaces talk to change, and building them every time
# src/ is touched is work nobody asked for. CI names both.
add_executable(lens_qtest_components EXCLUDE_FROM_ALL
  main.cpp
  setup.cpp
  setup.h
)
target_link_libraries(lens_qtest_components PRIVATE
  lens_app lens_appplugin Qt6::QuickTest
)
target_compile_definitions(lens_qtest_components PRIVATE
  LENS_QTEST_NAME="lens_qtest_components"
  LENS_QTEST_DIR="${CMAKE_SOURCE_DIR}/test/qtest/components"
  LENS_SYSTEM_FONTS="${LENS_SYSTEM_FONTS}"
)

# The surfaces: Main.qml and the nine windows it owns, which read Controller and Tray from the
# first binding they evaluate. setup.cpp builds both the way main() does -- see its file header
# for the two steps it deliberately skips -- and feeds them a checked-in fixture rather than
# the developer's settings.local.json.
add_executable(lens_qtest_surfaces EXCLUDE_FROM_ALL
  main.cpp
  setup.cpp
  setup.h
)
# lens_core comes along for its include directories: setup.cpp builds a KnownStore, which
# needs nlohmann/json.hpp from third_party/. lens_app links it privately, so it has to be
# named here as well.
target_link_libraries(lens_qtest_surfaces PRIVATE
  lens_app lens_core lens_appplugin Qt6::QuickTest
)
target_compile_definitions(lens_qtest_surfaces PRIVATE
  LENS_QTEST_NAME="lens_qtest_surfaces"
  LENS_QTEST_DIR="${CMAKE_SOURCE_DIR}/test/qtest/surfaces"
  LENS_QTEST_SINGLETONS
  LENS_SETTINGS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/surfaces/settings.json"
  # The price list the cost surfaces are formatted from, read from the same place the
  # application reads it.
  LENS_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
  LENS_SYSTEM_FONTS="${LENS_SYSTEM_FONTS}"
)
