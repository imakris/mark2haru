cmake_minimum_required(VERSION 3.20)

# Consumes mark2haru the way an outside project does: through the installed
# package, find_package and the exported target, with no access to this
# repository's source or build tree.
project(mark2haru_consumer_smoke LANGUAGES CXX)

find_package(mark2haru CONFIG REQUIRED)

add_executable(consumer_smoke consumer.cpp)
target_link_libraries(consumer_smoke PRIVATE mark2haru::mark2haru)

# One predictable location for the binary on both single- and multi-config
# generators, so the caller does not have to guess a per-configuration path.
set_target_properties(consumer_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
foreach(_config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${_config}" _config_upper)
    set_target_properties(consumer_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_config_upper} "${CMAKE_BINARY_DIR}/bin"
    )
endforeach()
