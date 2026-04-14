# Minimal PDF smoke check: verify the file exists, is non-empty, and starts
# with the PDF magic "%PDF-".
if(NOT DEFINED PDF)
    message(FATAL_ERROR "check_pdf_header.cmake requires -DPDF=<path>")
endif()

if(NOT EXISTS "${PDF}")
    message(FATAL_ERROR "PDF not produced: ${PDF}")
endif()

file(SIZE "${PDF}" _pdf_size)
if(_pdf_size LESS 64)
    message(FATAL_ERROR "PDF suspiciously small (${_pdf_size} bytes): ${PDF}")
endif()

file(READ "${PDF}" _pdf_head LIMIT 5 HEX)
# "%PDF-" in hex.
if(NOT _pdf_head STREQUAL "25504446 2d" AND NOT _pdf_head STREQUAL "255044462d")
    message(FATAL_ERROR "PDF header mismatch for ${PDF}: got hex '${_pdf_head}'")
endif()

message(STATUS "OK: ${PDF} (${_pdf_size} bytes)")
