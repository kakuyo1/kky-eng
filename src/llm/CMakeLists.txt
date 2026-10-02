add_library(lens_llm STATIC
  llm_pure.cpp
  llm_client.cpp
)

# src/ is exposed for self-contained "llm/xxx.h" includes (same as lens_core).
target_include_directories(lens_llm PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Qt is part of the public surface (QObject, QString, QUrl); lens_core is not.
# lens_core is a private dependency: it supplies core/log.h and spdlog.
target_link_libraries(lens_llm
  PUBLIC Qt6::Core Qt6::Network
  PRIVATE lens_core
)
