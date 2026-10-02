# The selection entry point lands before AppController and the QML surfaces do: the
# low-level mouse hook that notices a finished selection, and the grabber that copies the
# text out of the foreground application.
#
# Qt6::Core only for now -- QPoint, QEventLoop and QTimer all live there, and nothing here
# needs a window. Gui, Quick and Widgets join when the QML surfaces land, which is also when
# this gains qt_add_qml_module. The contract's link line (PHASE1.md 4.4) describes that
# finished state, not this one.
add_library(lens_app STATIC
  mouse_selection_hook.cpp
  selection_text_grabber.cpp
)

# src/ is exposed for self-contained "app/xxx.h" includes (same as lens_core and lens_llm).
target_include_directories(lens_app PUBLIC ${CMAKE_SOURCE_DIR}/src)

# lens_core is a private dependency: it supplies core/log.h and spdlog.
target_link_libraries(lens_app
  PUBLIC Qt6::Core
  PRIVATE lens_core
)

# user32 for the hook and SendInput, ole32 for the clipboard snapshot. Not guarded by
# if(WIN32): this module *is* the Win32 one, and the product has no other platform.
target_link_libraries(lens_app PRIVATE user32 ole32)
