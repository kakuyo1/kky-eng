add_library(lens_update STATIC
  update_pure.cpp
  update_client.cpp
)

# src/ is exposed for self-contained "update/xxx.h" includes (same as lens_llm and lens_core).
target_include_directories(lens_update PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Qt is part of the public surface (QObject, QString, QUrl); lens_util is not.
target_link_libraries(lens_update
  PUBLIC Qt6::Core Qt6::Network
  PRIVATE lens_util
)

# The version this build is. It is the left side of every update comparison, the User-Agent the
# request carries, and what the duty reads as the current version, so there is one number and it
# comes from the root project() rather than from a literal that goes stale at packaging time.
# PUBLIC because the duty in lens_app reads it too, and it is the same number for both.
target_compile_definitions(lens_update PUBLIC LENS_VERSION="${PROJECT_VERSION}")