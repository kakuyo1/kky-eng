# AppController and the QML surfaces belong to the next slice and are not implemented yet.
# The INTERFACE placeholder keeps the top-level configure working; landing the slice means
# switching this to STATIC, attaching QML resources via qt_add_qml_module, and adding sources.
add_library(lens_app INTERFACE)
target_link_libraries(lens_app INTERFACE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Widgets)
