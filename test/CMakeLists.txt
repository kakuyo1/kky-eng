# Self-check target: built on the host only, never shipped.
add_executable(lens_test main.cpp)
target_link_libraries(lens_test PRIVATE lens_core lens_llm)

# The self-check reads the corpus and the word list from the repository, so the source
# directory is baked in as an absolute path: the result must not depend on the cwd.
target_compile_definitions(lens_test PRIVATE LENS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
