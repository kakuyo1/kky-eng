add_library(lens_llm STATIC
  # The file lives in src/util/, but lens_util's contract is Qt-free (PHASE2.md 7.1), so
  # lens_llm is the lowest target that can compile it.
  ${CMAKE_SOURCE_DIR}/src/util/qt_log.cpp
  llm_protocol.cpp
  llm_pricing.cpp
  llm_pure.cpp
  llm_client.cpp
)

# src/ is exposed for self-contained "llm/xxx.h" includes (same as lens_core).
target_include_directories(lens_llm PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Qt is part of the public surface (QObject, QString, QUrl); lens_util is not.
target_link_libraries(lens_llm
  PUBLIC Qt6::Core Qt6::Network
  PRIVATE lens_util
)
