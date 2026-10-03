# The app itself. Three groups of things live here:
#
#   lens_app  -- the C++ the surfaces are driven by: the mouse hook, the grabber, the
#                controller, and the tray. Qt Core / Gui / Widgets, no QML.
#   lens      -- the executable: main.cpp plus the QML module the surfaces live in.
#   icons/    -- the tray art, compiled into the binary as resources.
#
# The QML module hangs off the executable rather than off lens_app: attaching one to a static
# library means Qt's QML plugin registration has to be handled by hand, and the test targets
# that link lens_app have no use for QML anyway (PHASE1.md section 7).

add_library(lens_app STATIC
  mouse_selection_hook.cpp
  selection_text_grabber.cpp
  app_controller.cpp
  tray.cpp
)

# src/ is exposed for self-contained "app/xxx.h" includes (same as lens_core and lens_llm).
target_include_directories(lens_app PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Qt6::Gui is public because AppController's interface reaches into it: the clipboard and
# QPoint come from there. Widgets is public for the tray -- QMenu and QSystemTrayIcon.
# lens_llm is public for the same reason: the constructor takes an LlmClient and a Pricing.
target_link_libraries(lens_app
  PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets lens_llm
  PRIVATE lens_core
)

# user32 for the hook, SendInput and the clipboard. Not guarded by if(WIN32): this module
# *is* the Win32 one, and the product has no other platform. OLE is deliberately absent --
# ClipboardSnapshot records why its round trip is not usable here.
target_link_libraries(lens_app PRIVATE user32)

qt_add_executable(lens
  main.cpp
)

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

# Where the word list, the protocol files and the settings document live.
#
# ponytail: absolute paths, baked in at configure time. This is a development build; a
# shipped one needs the data in a resource and the settings somewhere under the user's
# profile, which is a packaging change rather than a code one.
target_compile_definitions(lens PRIVATE
  LENS_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
  LENS_SETTINGS_PATH="${CMAKE_SOURCE_DIR}/settings.local.json"
)

# The token table is a single instance every surface reads, not one per surface. The
# property has to be set before qt_add_qml_module looks at the file, not after: it decides
# there whether to write a singleton entry into the module's qmldir.
set_source_files_properties(qml/components/Tokens.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Two groups, split by what the file is rather than by where it is used. The nine windows are
# the surfaces themselves -- one per thing the reader sees -- and they sit at the root so that
# Main stays loadFromModule("Lens", "Main"). Everything else is a component a surface is built
# out of, and lives in qml/components/. Both are the same QML module, so neither group names the
# other in an import: Qt gives every file in the module an implicit import of its own types.
qt_add_qml_module(lens
  URI Lens
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/TrayMenu.qml
    qml/SelectionBar.qml
    qml/Bubble.qml
    qml/SettingsPopup.qml
    qml/StatsPopup.qml
    qml/WordsPopup.qml
    qml/CostPopup.qml
    qml/SendConfirm.qml
    qml/components/Tokens.qml
    qml/components/Icon.qml
    qml/components/ShadowCard.qml
    qml/components/Segment.qml
    qml/components/Switch.qml
    qml/components/SwitchRow.qml
    qml/components/StatRow.qml
    qml/components/DropdownField.qml
    qml/components/MenuRow.qml
)

# The icon art, addressed as :/icons/<name>.svg. BASE strips the source directory so the names
# stay as written on disk. The tray-<state>-<taskbar> files are the shell's bitmap states, each
# carrying its own two colours; the ui- files are the surfaces' outlines, stroked in white and
# tinted from QML, so one file serves every surface and both themes.
set(LENS_ICONS
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
  ${CMAKE_SOURCE_DIR}/icons/ui-language.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-quit.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-settings.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-stats.svg
  ${CMAKE_SOURCE_DIR}/icons/ui-translate.svg
)
qt_add_resources(lens "icons"
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
  RESOURCE_PREFIX "/i18n"
)

