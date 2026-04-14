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
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
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
