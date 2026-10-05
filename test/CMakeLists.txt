# Test targets: built on the host only, never shipped.
add_subdirectory(googletest)

# The Qt side: the components and the surfaces, driven through a real QML engine. Separate
# because a case there needs a QGuiApplication and a scene, which gtest has no room for.
add_subdirectory(qtest)
