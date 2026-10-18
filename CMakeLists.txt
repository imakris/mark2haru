# Font staging uses file(COPY_FILE), available since 3.21.
cmake_minimum_required(VERSION 3.21)

project(mark2haru VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(MARK2HARU_IS_TOP_LEVEL ON)
else()
    set(MARK2HARU_IS_TOP_LEVEL OFF)
endif()

option(MARK2HARU_BUILD_CLI "Build the mark2haru command-line tool" ${MARK2HARU_IS_TOP_LEVEL})
option(MARK2HARU_BUILD_TESTS "Build mark2haru tests" ${MARK2HARU_IS_TOP_LEVEL})

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
set(MARK2HARU_INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}/mark2haru")

# One owner stages all registered destinations serially: independent copy
# commands can race when several consumers share an output directory. Resolve
# and deduplicate the destinations at generation time, since configuration
# properties and generator expressions can split or merge them. Building any
# consumer stages fonts for all registered consumers in that configuration.
function(mark2haru_stage_bundled_fonts target)
    set(_mark2haru_owner mark2haru_bundled_fonts)
    if(NOT TARGET ${_mark2haru_owner})
        set(_mark2haru_font_paths "")
        foreach(_mark2haru_font IN LISTS MARK2HARU_BUNDLED_FONT_FILES)
            list(APPEND _mark2haru_font_paths
                "${MARK2HARU_BUNDLED_FONT_DIR}/${_mark2haru_font}")
        endforeach()

        set(_mark2haru_stage_script
            "${CMAKE_CURRENT_BINARY_DIR}/mark2haru_bundled_fonts_$<CONFIG>.cmake")
        add_custom_target(${_mark2haru_owner}
            COMMAND ${CMAKE_COMMAND} -P "${_mark2haru_stage_script}"
            COMMENT "Staging mark2haru bundled fonts"
            VERBATIM
        )

        # Keep the property lookup deferred so parent and sibling directories
        # can register consumers after this function's first call.
        set(_mark2haru_registered_destinations
            "$<TARGET_PROPERTY:${_mark2haru_owner},MARK2HARU_FONT_DESTINATIONS>")
        set(_mark2haru_destinations
            "$<TARGET_GENEX_EVAL:${_mark2haru_owner},${_mark2haru_registered_destinations}>")
        string(CONCAT _mark2haru_stage_content
            "set(font_files [==[${_mark2haru_font_paths}]==])\n"
            "set(font_destinations [==[$<REMOVE_DUPLICATES:${_mark2haru_destinations}>]==])\n"
            "include([==[${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cmake/stage_bundled_fonts.cmake]==])\n")
        file(GENERATE OUTPUT "${_mark2haru_stage_script}"
            CONTENT "${_mark2haru_stage_content}")
    endif()

    set_property(TARGET ${_mark2haru_owner} APPEND PROPERTY
        MARK2HARU_FONT_DESTINATIONS "$<TARGET_FILE_DIR:${target}>/fonts")
    add_dependencies(${target} ${_mark2haru_owner})
endfunction()

function(mark2haru_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(mark2haru_miniz OBJECT
    # miniz_zip.c is intentionally excluded - we only use compress2/compressBound
    # from miniz.c (with deflate from miniz_tdef.c). The zip archive support
    # pulls in a lot of unused code.
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
)

target_include_directories(mark2haru_miniz PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
)

add_library(mark2haru_core STATIC
    src/font_context.cpp
    src/markdown.cpp
    src/pdf_writer.cpp
    src/png_image.cpp
    src/render.cpp
    src/table_layout.cpp
    src/ttf_font.cpp
    $<TARGET_OBJECTS:mark2haru_miniz>
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

mark2haru_enable_warnings(mark2haru_core)

if(MARK2HARU_BUILD_CLI)
    add_executable(mark2haru
        src/main.cpp
    )

    target_link_libraries(mark2haru PRIVATE mark2haru_core)

    target_compile_definitions(mark2haru PRIVATE
        MARK2HARU_INSTALL_DATADIR="${MARK2HARU_INSTALL_DATADIR}"
    )

    mark2haru_enable_warnings(mark2haru)

    if(MINGW)
        target_link_options(mark2haru PRIVATE -municode)
    endif()

    mark2haru_stage_bundled_fonts(mark2haru)
endif()

install(TARGETS mark2haru_core
    EXPORT mark2haruTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(MARK2HARU_BUILD_CLI)
    install(TARGETS mark2haru
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY fonts/
    DESTINATION ${MARK2HARU_INSTALL_DATADIR}/fonts
    FILES_MATCHING PATTERN "*.ttf"
)

install(FILES fonts/LICENSE.DejaVu
    DESTINATION ${MARK2HARU_INSTALL_DATADIR}/fonts
)

install(FILES third_party/miniz/LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/third_party/miniz
)

install(EXPORT mark2haruTargets
    NAMESPACE mark2haru::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mark2haru
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mark2haruConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mark2haruConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mark2haru
    PATH_VARS MARK2HARU_INSTALL_DATADIR
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
    function(mark2haru_add_test_executable target)
        add_executable(${target} ${ARGN})
        target_link_libraries(${target} PRIVATE mark2haru_core)
        mark2haru_enable_warnings(${target})
    endfunction()

    mark2haru_add_test_executable(test_failure_paths tests/test_failure_paths.cpp)
    mark2haru_add_test_executable(test_pdf_writer tests/test_pdf_writer.cpp)
    mark2haru_add_test_executable(test_png tests/test_png.cpp)
    mark2haru_add_test_executable(test_markdown tests/test_markdown.cpp)
    mark2haru_add_test_executable(test_table_layout tests/test_table_layout.cpp)
    mark2haru_add_test_executable(test_text_layout tests/test_text_layout.cpp)
    mark2haru_add_test_executable(test_render_pagination tests/test_render_pagination.cpp)

    if(TARGET mark2haru)
        mark2haru_add_test_executable(test_cli_relative_image tests/test_cli_relative_image.cpp)
    endif()

    set(_mark2haru_miniz_test_targets test_pdf_writer test_png test_render_pagination)
    if(TARGET mark2haru)
        list(APPEND _mark2haru_miniz_test_targets test_cli_relative_image)
    endif()
    foreach(_mark2haru_test_target IN LISTS _mark2haru_miniz_test_targets)
        target_include_directories(${_mark2haru_test_target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
        )
    endforeach()

    mark2haru_stage_bundled_fonts(test_pdf_writer)
    mark2haru_stage_bundled_fonts(test_png)
    mark2haru_stage_bundled_fonts(test_table_layout)
    mark2haru_stage_bundled_fonts(test_render_pagination)

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
        set(MARK2HARU_CLI_RELATIVE_IMAGE_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/cli_relative_image")
        add_test(
            NAME cli_relative_image_setup
            COMMAND test_cli_relative_image
                setup
                ${MARK2HARU_CLI_RELATIVE_IMAGE_DIR}
        )
        add_test(
            NAME cli_relative_image
            COMMAND mark2haru
                ${MARK2HARU_CLI_RELATIVE_IMAGE_DIR}/input/doc.md
                ${MARK2HARU_CLI_RELATIVE_IMAGE_DIR}/out.pdf
        )
        set_tests_properties(cli_relative_image PROPERTIES
            DEPENDS cli_relative_image_setup
            WORKING_DIRECTORY ${MARK2HARU_CLI_RELATIVE_IMAGE_DIR}/run
        )
        add_test(
            NAME cli_relative_image_check
            COMMAND test_cli_relative_image
                check
                ${MARK2HARU_CLI_RELATIVE_IMAGE_DIR}/out.pdf
        )
        set_tests_properties(cli_relative_image_check PROPERTIES
            DEPENDS cli_relative_image
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
    add_test(
        NAME table_layout
        COMMAND test_table_layout
    )
    add_test(
        NAME text_layout
        COMMAND test_text_layout
    )
    add_test(
        NAME render_pagination
        COMMAND test_render_pagination
    )
endif()
