cmake_minimum_required(VERSION 3.20)

project(mark2haru VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(MARK2HARU_BUILD_CLI "Build the mark2haru command-line tool" ${PROJECT_IS_TOP_LEVEL})
option(MARK2HARU_BUILD_TESTS "Build mark2haru tests" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MARK2HARU_BUNDLED_FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
    CACHE INTERNAL "Directory containing mark2haru bundled runtime fonts")
set(MARK2HARU_BUNDLED_FONT_FILES
    DejaVuSans.ttf
    DejaVuSans-Bold.ttf
    DejaVuSans-Oblique.ttf
    DejaVuSans-BoldOblique.ttf
    DejaVuSansMono.ttf
    CACHE INTERNAL "mark2haru bundled runtime font filenames")

function(mark2haru_copy_bundled_fonts target destination_dir)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination_dir}"
        VERBATIM
    )
    foreach(_mark2haru_font IN LISTS MARK2HARU_BUNDLED_FONT_FILES)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MARK2HARU_BUNDLED_FONT_DIR}/${_mark2haru_font}"
                "${destination_dir}/${_mark2haru_font}"
            VERBATIM
        )
    endforeach()
endfunction()

add_library(mark2haru_core STATIC
    src/font_context.cpp
    src/markdown.cpp
    src/pdf_writer.cpp
    src/png_image.cpp
    src/render.cpp
    src/table_layout.cpp
    src/ttf_font.cpp
    # miniz_zip.c is intentionally excluded - we only use compress2/compressBound
    # from miniz.c (with deflate from miniz_tdef.c). The zip archive support
    # pulls in a lot of unused code.
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
)

add_library(mark2haru::mark2haru ALIAS mark2haru_core)

set_target_properties(mark2haru_core PROPERTIES
    EXPORT_NAME mark2haru
)

target_compile_features(mark2haru_core PUBLIC cxx_std_17)

target_include_directories(mark2haru_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
)

if (MSVC)
    target_compile_options(mark2haru_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(mark2haru_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MARK2HARU_BUILD_CLI)
    add_executable(mark2haru
        src/main.cpp
    )

    target_link_libraries(mark2haru PRIVATE mark2haru_core)

    if(MSVC)
        target_compile_options(mark2haru PRIVATE /W4 /permissive-)
    else()
        target_compile_options(mark2haru PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_options(mark2haru PRIVATE -municode)
    endif()

    mark2haru_copy_bundled_fonts(mark2haru "$<TARGET_FILE_DIR:mark2haru>/fonts")
endif()

install(TARGETS mark2haru_core
    EXPORT mark2haruTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY fonts/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mark2haru/fonts
    FILES_MATCHING PATTERN "*.ttf"
)

install(EXPORT mark2haruTargets
    NAMESPACE mark2haru::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mark2haru
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mark2haruConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mark2haruConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mark2haru
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mark2haruConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mark2haruConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mark2haruConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mark2haru
)

# Smoke tests: render each bundled example and verify the output starts with
# "%PDF-". This catches silent breakage in the markdown parser, wrap engine,
# and PDF writer without pinning to byte-exact golden files.
if(MARK2HARU_BUILD_TESTS)
    add_executable(test_failure_paths
        tests/test_failure_paths.cpp
    )
    add_executable(test_pdf_writer
        tests/test_pdf_writer.cpp
    )
    add_executable(test_png
        tests/test_png.cpp
    )
    add_executable(test_markdown
        tests/test_markdown.cpp
    )

    target_link_libraries(test_failure_paths PRIVATE mark2haru_core)
    target_link_libraries(test_pdf_writer PRIVATE mark2haru_core)
    target_link_libraries(test_png PRIVATE mark2haru_core)
    target_link_libraries(test_markdown PRIVATE mark2haru_core)
    target_include_directories(test_pdf_writer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
    )
    target_include_directories(test_png PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
    )
    mark2haru_copy_bundled_fonts(test_pdf_writer "$<TARGET_FILE_DIR:test_pdf_writer>/fonts")

    if(MSVC)
        target_compile_options(test_failure_paths PRIVATE /W4 /permissive-)
        target_compile_options(test_pdf_writer PRIVATE /W4 /permissive-)
        target_compile_options(test_png PRIVATE /W4 /permissive-)
        target_compile_options(test_markdown PRIVATE /W4 /permissive-)
    else()
        target_compile_options(test_failure_paths PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(test_pdf_writer PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(test_png PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(test_markdown PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    enable_testing()

    if(TARGET mark2haru)
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
    endif()

    add_test(
        NAME png_roundtrip
        COMMAND test_png
    )
    add_test(
        NAME pdf_writer_primitives
        COMMAND test_pdf_writer
    )
    add_test(
        NAME failure_paths
        COMMAND test_failure_paths
    )
    add_test(
        NAME markdown_parse
        COMMAND test_markdown
    )
endif()
