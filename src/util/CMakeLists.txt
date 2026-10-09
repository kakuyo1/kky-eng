add_library(lens_util STATIC
  text.cpp
  log.cpp
)

target_include_directories(lens_util PUBLIC ${CMAKE_SOURCE_DIR}/src)

# spdlog is PUBLIC: util/log.h is the logging entry point for every other module.
target_link_libraries(lens_util PUBLIC spdlog::spdlog)

# Calls below this level compile away. It belongs here rather than in log.h: spdlog's
# common.h supplies its own info default as soon as it is included, so a header-side
# definition loses to it. Passed as the token SPDLOG_LEVEL_TRACE, which the preprocessor
# resolves once spdlog's constants exist.
target_compile_definitions(lens_util PUBLIC
  $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
  $<$<NOT:$<CONFIG:Debug>>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
)
