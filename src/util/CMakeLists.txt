add_library(lens_util STATIC
  text.cpp
)

target_include_directories(lens_util PUBLIC ${CMAKE_SOURCE_DIR}/src)
