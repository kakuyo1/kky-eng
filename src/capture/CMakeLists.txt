add_library(lens_capture STATIC
  capture_policy.h
  scan_pipeline.h
  scan_pipeline.cpp
  ocr_engine.h
  ocr_engine.cpp
  foreground_window.h
  foreground_window.cpp
)

target_compile_features(lens_capture PUBLIC cxx_std_20)
target_include_directories(lens_capture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(lens_capture PUBLIC lens_core Qt6::Core Qt6::Gui)
target_link_libraries(lens_capture PRIVATE user32)

if(LENS_OCR_DEV_PATH_FALLBACK)
  target_compile_definitions(lens_capture PRIVATE LENS_OCR_DEV_PATH_FALLBACK)
endif()
