# The app itself. Three groups of things live here:
#
#   lens_app  -- the C++ the surfaces are driven by: the mouse hook, the grabber, the
#                controller, and the tray -- plus the QML module those two types are registered
#                in.
#   lens      -- the executable: main.cpp and the .qm files.
#   icons/    -- the tray art, compiled into the binary as resources.
#
# The QML module hangs off lens_app and not off the executable, which is the reverse of what
# this file used to say. The reason is registration scope: qt_add_qml_module registers the C++
# types of its own backing target and no others, so a module attached to `lens` could not
# register classes that live in lens_app.

add_library(lens_app STATIC
  capture_duty.h
  capture_duty.cpp
  cost_duty.h
  cost_duty.cpp
  explanation_duty.h
  explanation_duty.cpp
  global_hotkey.h
  global_hotkey.cpp
  mouse_selection_hook.h
  mouse_selection_hook.cpp
  selection_text_grabber.cpp
  storage_duty.h
  storage_duty.cpp
  system_motion.cpp
  app_controller.cpp
  autostart.cpp
  tray.cpp
)

# src/ is exposed for self-contained "app/xxx.h" includes (same as lens_core and lens_llm).
target_include_directories(lens_app PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Qt6::Gui is public because AppController's interface reaches into it: the clipboard and
# QPoint come from there. Widgets is public for the tray -- QMenu and QSystemTrayIcon.
# lens_llm is public for the same reason: the constructor takes an LlmClient and a Pricing.
# Quick is public because the QML module below backs this library, and anything linking it is
# linking the module: that now includes lens_gtest_integration, which used to need no QML.
target_link_libraries(lens_app
  PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick lens_llm
  PRIVATE lens_core lens_util lens_capture
)

# user32 for the hook, SendInput and the clipboard. Not guarded by if(WIN32): this module
# *is* the Win32 one, and the product has no other platform. OLE is deliberately absent --
# ClipboardSnapshot records why its round trip is not usable here.
target_link_libraries(lens_app PRIVATE user32)

# RC, so the resource script below compiles. The root project() lists CXX alone and the root
# CMakeLists is another branch's file, but enable_language is directory scoped: turning it on
# here covers this directory and everything under it, and this is the only directory with a
# resource script to compile.
enable_language(RC)

# main.cpp, the .qm files, and the icon the shell draws for lens.exe. lens.rc names lens.ico
# by a path relative to itself, so the resource compiler reads the art beside the script and
# no machine path is written into either file.
qt_add_executable(lens
  main.cpp
  ${CMAKE_SOURCE_DIR}/icons/lens.rc
)

# The module's QML files reach the executable through lens_app's resources, but the C++ types
# it registers do not come with them: a static plugin is only imported when its init object --
# the one carrying Q_IMPORT_PLUGIN -- is linked in, and nothing links it on its own. Qt attaches
# that object to the plugin target, so naming the plugin is the whole fix. Without it the
# surfaces load and then every reference to Controller or Tray is a ReferenceError.
target_link_libraries(lens PRIVATE lens_appplugin)

# GUI subsystem, not console. Left at the default, a tray application carries a console window
# on screen for as long as it runs -- there is no main window to close it with. The property is
# also what pulls Qt6::EntryPoint in: Qt6::Core's interface links it through
# $<BOOL:$<TARGET_PROPERTY:WIN32_EXECUTABLE>>, so setting this one property is the whole change.
set_target_properties(lens PROPERTIES WIN32_EXECUTABLE ON)

# lens_core comes along for its include directories: main.cpp reads the settings document
# through KnownStore, which needs nlohmann/json.hpp from third_party/.
target_link_libraries(lens
  PRIVATE lens_app lens_core Qt6::Quick Qt6::Svg
)

# The two paths a build tree has and an installed copy does not. Neither is the only answer
# any more: main.cpp prefers data/ beside the executable, which is where installer/lens.iss
# puts it, and keeps the settings document in %APPDATA% -- these are the fallbacks that keep a
# build tree working, where nothing writes either one next to build-ninja/src/app/lens.exe.
#
# LENS_DATA_DIR is where the word list and the protocol files are read from when there is no
# data/ beside the executable. LENS_SETTINGS_PATH is the document a first run after the
# packaging change carries into the profile once, so an API key already configured keeps
# working; nothing writes it afterwards.
target_compile_definitions(lens PRIVATE
  LENS_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
  LENS_SETTINGS_PATH="${CMAKE_SOURCE_DIR}/settings.local.json"
)

# The token table is a single instance every surface reads, not one per surface. The
# property has to be set before qt_add_qml_module looks at the file, not after: it decides
# there whether to write a singleton entry into the module's qmldir.
set_source_files_properties(qml/components/Tokens.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# The URI is Lens, so generated QML tooling files belong below a Lens directory. Keeping the
# target path aligned makes CMake generate the qmllint response file in every build tree,
# including the dedicated profiler tree.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Lens")

# Three groups, split by what the file is rather than by where it is used. The ten windows are
# the surfaces themselves -- one per thing the reader sees -- and they sit at the root so that
# Main stays loadFromModule("Lens", "Main"). Everything a surface is built out of lives in
# qml/components/, and the two theme value tables plus the motion table sit in qml/theme/, where
# nothing reads them by name: Tokens forwards the active one. All three are the same QML module, so no group names
# another in an import: Qt gives every file in the module an implicit import of its own types.
# The module hangs off lens_app and not off the executable because of where the two types the
# surfaces talk to are declared. qt_add_qml_module registers the C++ types of the target it is
# called on and only that target's; a class living in a linked static library stays invisible to
# it. A module on `lens` therefore registered nothing, and every read off Controller or Tray
# came back to qmllint as unqualified access -- 53 warnings that no amount of renaming fixes.
# The executable still owns main.cpp, the icon resources and the .qm files, and picks the
# module up by linking lens_app.
qt_add_qml_module(lens_app
  URI Lens
  VERSION 1.0
  SOURCES
    app_controller.h
    tray.h
    system_motion.h
  QML_FILES
    qml/Main.qml
    qml/TrayMenu.qml
    qml/SelectionBar.qml
    qml/Bubble.qml
    qml/Notice.qml
    qml/Mask.qml
    qml/SettingsPopup.qml
    qml/SettingsCategoryRow.qml
    qml/SettingsLinkRow.qml
    qml/SettingsCatalog.qml
    qml/SettingsBudgetPage.qml
    qml/SettingsGeneralPage.qml
    qml/SettingsLearningPage.qml
    qml/SettingsCapturePage.qml
    qml/SettingsModelPage.qml
    qml/SettingsExtensionsPage.qml
    qml/SettingsClipboardPage.qml
    qml/SettingsConnectionPage.qml
     qml/StatsPopup.qml
     qml/WordsPopup.qml
     qml/YearWordsPopup.qml
     qml/RemovalQuestion.qml
    qml/CostPopup.qml
    qml/components/Tokens.qml
    qml/theme/Light.qml
    qml/theme/Dark.qml
    qml/theme/Forest.qml
    qml/theme/Motion.qml
    qml/components/Icon.qml
    qml/components/ShadowCard.qml
    qml/components/Segment.qml
    qml/components/Switch.qml
    qml/components/SwitchRow.qml
    qml/components/StatRow.qml
    qml/components/DropdownField.qml
    qml/components/MenuRow.qml
    qml/components/PathField.qml
    qml/components/MixedText.qml
    qml/components/HotkeyField.qml
)

# The icon art, addressed as :/icons/<name>.svg. BASE strips the source directory so the names
# stay as written on disk. The tray-<state>-<taskbar> files are the shell's bitmap states, each
# carrying its own two colours; the ui- files are the surfaces' outlines, stroked in white and
# tinted from QML, so one file serves every surface and both themes.
#
# lens.svg is the product mark itself -- the ring and the centre dot every other file here is a
# recolouring of. It is stroked in white like the ui- set, so the tray menu's first line draws it
# through Icon and it takes the theme's colour; the tray art and lens.ico are its geometry with
# the ink baked in, since a shell icon has no QML to tint it. The .ico and the .rc that carries
# it are the executable's rather than resources, so they are named in the executable above.
#
# It hangs off lens_app and not off the executable, for the same reason the QML module does: a
# resource only resolves in a binary that carries it, and the surfaces are the ones reading these
# -- every QTest target links lens_app, so on `lens` each of them drew a missing image and printed
# a warning per case. The executable still gets them, since it links lens_app.
set(LENS_ICONS
  ${CMAKE_SOURCE_DIR}/icons/lens.svg
  ${CMAKE_SOURCE_DIR}/icons/tray-auto-ondark.svg
  ${CMAKE_SOURCE_DIR}/icons/tray-auto-onlight.svg
  ${CMAKE_SOURCE_DIR}/icons/tray-busy-ondark.svg
  ${CMAKE_SOURCE_DIR}/icons/tray-busy-onlight.svg
  ${CMAKE_SOURCE_DIR}/icons/tray-off-ondark.svg
  ${CMAKE_SOURCE_DIR}/icons/tray-off-onlight.svg
  ${CMAKE_SOURCE_DIR}/icons/tray-budget-ondark.svg
  ${CMAKE_SOURCE_DIR}/icons/tray-budget-onlight.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-back.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-chevron-down.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-chevron-right.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-close.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-copy.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-explain.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-export.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-folder.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-language.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-quit.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-settings.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-stats.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-translate.svg
)
qt_add_resources(lens_app "icons"
  PREFIX "/icons"
  BASE ${CMAKE_SOURCE_DIR}/icons
  FILES ${LENS_ICONS}
)

# The .ts files, run through lrelease and embedded at :/i18n/. Rebuilding after a string
# change needs lupdate first -- see CLAUDE.md, which owns that command.
qt_add_translations(lens
  TS_FILES
    ${CMAKE_SOURCE_DIR}/i18n/lens_en_US.ts
    ${CMAKE_SOURCE_DIR}/i18n/lens_zh_CN.ts
    ${CMAKE_SOURCE_DIR}/i18n/lens_es_ES.ts
    ${CMAKE_SOURCE_DIR}/i18n/lens_ja_JP.ts
  RESOURCE_PREFIX "/i18n"
)

