cmake_minimum_required(VERSION 3.21)
project(mark2haru_font_staging_tests LANGUAGES C CXX)

set(MARK2HARU_BUILD_CLI ON CACHE BOOL "" FORCE)
set(MARK2HARU_BUILD_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." mark2haru)

# Use the actual consumers and helper. A parent can choose their final output
# directories after add_subdirectory has registered them for font staging.
set_target_properties(mark2haru PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/debug cli"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/release shared"
)
set_target_properties(test_pdf_writer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/debug writer"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/release shared"
)
