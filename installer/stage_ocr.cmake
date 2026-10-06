# Stage the exact OCR runtime that the installed application resolves from its own directory.
# This script deliberately has no PATH fallback: a release either has a configured source or it
# fails before Inno Setup can produce an incomplete installer.

foreach(required_variable IN ITEMS LENS_OCR_SOURCE_DIR LENS_OCR_STAGE_DIR LENS_OCR_NOTICE_FILE)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "OCR staging requires -D${required_variable}=<value>")
  endif()
endforeach()

if(NOT IS_DIRECTORY "${LENS_OCR_SOURCE_DIR}")
  message(FATAL_ERROR
    "OCR runtime source is missing: '${LENS_OCR_SOURCE_DIR}'. "
    "Set tesseractRoot in config/paths.local.json to a Tesseract distribution.")
endif()

set(LENS_OCR_EXECUTABLE "${LENS_OCR_SOURCE_DIR}/tesseract.exe")
set(LENS_OCR_TRAINEDDATA "${LENS_OCR_SOURCE_DIR}/tessdata/eng.traineddata")
set(LENS_OCR_LICENSE "${LENS_OCR_SOURCE_DIR}/doc/LICENSE")

foreach(required_file IN ITEMS LENS_OCR_EXECUTABLE LENS_OCR_TRAINEDDATA LENS_OCR_LICENSE)
  if(NOT EXISTS "${${required_file}}")
    message(FATAL_ERROR
      "OCR runtime source is incomplete: '${${required_file}}' was not found under "
      "'${LENS_OCR_SOURCE_DIR}'. Configure tesseractRoot with a complete distribution.")
  endif()
endforeach()

file(GLOB LENS_OCR_DLLS LIST_DIRECTORIES false "${LENS_OCR_SOURCE_DIR}/*.dll")
list(SORT LENS_OCR_DLLS)
if(NOT LENS_OCR_DLLS)
  message(FATAL_ERROR
    "OCR runtime source is incomplete: no DLLs were found under '${LENS_OCR_SOURCE_DIR}'.")
endif()
list(LENGTH LENS_OCR_DLLS LENS_OCR_DLL_COUNT)

if(NOT EXISTS "${LENS_OCR_NOTICE_FILE}")
  message(FATAL_ERROR "OCR redistribution notice is missing: '${LENS_OCR_NOTICE_FILE}'.")
endif()

file(MAKE_DIRECTORY "${LENS_OCR_STAGE_DIR}/tessdata")
file(COPY "${LENS_OCR_EXECUTABLE}" DESTINATION "${LENS_OCR_STAGE_DIR}")
file(COPY "${LENS_OCR_TRAINEDDATA}" DESTINATION "${LENS_OCR_STAGE_DIR}/tessdata")
file(COPY "${LENS_OCR_LICENSE}" DESTINATION "${LENS_OCR_STAGE_DIR}" FILE_PERMISSIONS
     OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
file(COPY "${LENS_OCR_NOTICE_FILE}" DESTINATION "${LENS_OCR_STAGE_DIR}" FILE_PERMISSIONS
     OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

foreach(dll IN LISTS LENS_OCR_DLLS)
  file(COPY "${dll}" DESTINATION "${LENS_OCR_STAGE_DIR}")
endforeach()

message(STATUS "Staged Tesseract OCR runtime and ${LENS_OCR_DLL_COUNT} DLLs under '${LENS_OCR_STAGE_DIR}'")
