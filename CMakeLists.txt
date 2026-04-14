cmake_minimum_required(VERSION 3.20)

project(mark2haru LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mark2haru
    src/main.cpp
    src/markdown.cpp
    src/pdf_writer.cpp
    src/render.cpp
    src/ttf_font.cpp
    # We only use compress2/compressBound for PDF stream compression, so
    # miniz_zip.c (archive format) is deliberately excluded. miniz_tinfl.c
    # (inflate) is still linked because miniz.c's mz_inflate shim references
    # tinfl_decompress unconditionally — removing it would require patching
    # upstream. The tradeoff: ~35 KB of dead code kept for source cleanliness.
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
)

target_include_directories(mark2haru PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
)

if (MSVC)
    target_compile_options(mark2haru PRIVATE /W4 /permissive-)
else()
    target_compile_options(mark2haru PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET mark2haru POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:mark2haru>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts/DejaVuSans.ttf
        $<TARGET_FILE_DIR:mark2haru>/fonts/DejaVuSans.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts/DejaVuSans-Bold.ttf
        $<TARGET_FILE_DIR:mark2haru>/fonts/DejaVuSans-Bold.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts/DejaVuSans-Oblique.ttf
        $<TARGET_FILE_DIR:mark2haru>/fonts/DejaVuSans-Oblique.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts/DejaVuSans-BoldOblique.ttf
        $<TARGET_FILE_DIR:mark2haru>/fonts/DejaVuSans-BoldOblique.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts/DejaVuSansMono.ttf
        $<TARGET_FILE_DIR:mark2haru>/fonts/DejaVuSansMono.ttf
)

# Smoke tests: render each bundled example and verify the output starts with
# "%PDF-". This catches silent breakage in the markdown parser, wrap engine,
# and PDF writer without pinning to byte-exact golden files.
enable_testing()

add_test(
    NAME smoke_render_neutral_brief
    COMMAND mark2haru
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/neutral_brief.md
        ${CMAKE_CURRENT_BINARY_DIR}/neutral_brief.pdf
)
add_test(
    NAME smoke_check_neutral_brief_header
    COMMAND ${CMAKE_COMMAND}
        -DPDF=${CMAKE_CURRENT_BINARY_DIR}/neutral_brief.pdf
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_pdf_header.cmake
)
set_tests_properties(smoke_check_neutral_brief_header PROPERTIES
    DEPENDS smoke_render_neutral_brief
)

add_test(
    NAME smoke_render_western_european
    COMMAND mark2haru
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/western_european.md
        ${CMAKE_CURRENT_BINARY_DIR}/western_european.pdf
)
add_test(
    NAME smoke_check_western_european_header
    COMMAND ${CMAKE_COMMAND}
        -DPDF=${CMAKE_CURRENT_BINARY_DIR}/western_european.pdf
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_pdf_header.cmake
)
set_tests_properties(smoke_check_western_european_header PROPERTIES
    DEPENDS smoke_render_western_european
)
