# gtest covers everything up to and including Qt Core: lens_core, and the LlmClient helpers
# that are Qt types but need no window. QML and widget tests arrive under test/qtest with
# QTest targets of their own.
#
# integration/ and e2e/ have no cases yet, so they have no directory -- git does not track
# empty ones. Add the folder and an add_executable here when the first case lands.
#
# There is no ctest registration: the executables are run directly, as CLAUDE.md documents.
# gtest_discover_tests() would run each one during the build, which needs the Qt DLLs on
# PATH at build time and is not worth the coupling.

# Every target below reads the corpus, the word list and the protocol from the repository, so
# the source directory is baked in as an absolute path: the result must not depend on the cwd.
function(lens_gtest_configure target)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${target} PRIVATE LENS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endfunction()

# The offline suite: no network, no API key, safe for CI. This is the one to run by default.
add_executable(lens_gtest_unit
  unit/filter_core_test.cpp
  unit/llm_pure_test.cpp
)
target_link_libraries(lens_gtest_unit PRIVATE lens_core lens_llm GTest::gtest_main)
lens_gtest_configure(lens_gtest_unit)

# Prints throughput and the profile breakdown; see the file header. Its only assertion is
# that the workload reached the pipeline, so this is a measurement, not a gate.
add_executable(lens_gtest_perf
  perf/filter_perf_test.cpp
)
target_link_libraries(lens_gtest_perf PRIVATE lens_core GTest::gtest_main)
lens_gtest_configure(lens_gtest_perf)

# Needs a key and spends money, so it is never part of the default run: it reports a skip
# when settings.local.json has no credentials. Supplies its own main() to get an event loop,
# hence GTest::gtest rather than GTest::gtest_main.
add_executable(lens_gtest_smoke
  smoke/llm_smoke_test.cpp
)
target_link_libraries(lens_gtest_smoke PRIVATE lens_core lens_llm GTest::gtest)
lens_gtest_configure(lens_gtest_smoke)
