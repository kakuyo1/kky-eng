add_library(lens_core STATIC
  log.cpp
  filter_core.cpp
  known_store.cpp
)

# src/ is exposed for self-contained "core/xxx.h" includes; third_party/ for nlohmann/json.hpp
target_include_directories(lens_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party
)

# spdlog is PUBLIC: core/log.h is the logging entry point for every other module.
target_link_libraries(lens_core PUBLIC spdlog::spdlog)
