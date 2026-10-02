add_library(lens_core STATIC
  log.cpp
  profile.cpp
  filter_core.cpp
  known_store.cpp
  stats_store.cpp
)

# src/ is exposed for self-contained "core/xxx.h" includes; third_party/ for nlohmann/json.hpp
target_include_directories(lens_core PUBLIC
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party
)

# spdlog is PUBLIC: core/log.h is the logging entry point for every other module.
target_link_libraries(lens_core PUBLIC spdlog::spdlog)

# Calls below this level compile away. It belongs here rather than in log.h: spdlog's
# common.h supplies its own info default as soon as it is included, so a header-side
# definition loses to it. Passed as the token SPDLOG_LEVEL_TRACE, which the preprocessor
# resolves once spdlog's constants exist.
target_compile_definitions(lens_core PUBLIC
  $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
  $<$<NOT:$<CONFIG:Debug>>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
)

# Profiling (core/profile.h) is off unless the build asks for it. PUBLIC, not PRIVATE: the
# macro decides whether a call site emits a sample, so lens_core and its callers must agree
# on the value or the reads and the writes would disagree about which sites exist.
target_compile_definitions(lens_core PUBLIC
  $<$<BOOL:${LENS_ENABLE_PROFILE}>:LENS_PROFILE=1>
)
