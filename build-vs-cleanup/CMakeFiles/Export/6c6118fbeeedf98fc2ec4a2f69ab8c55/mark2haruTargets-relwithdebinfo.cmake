#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "mark2haru::mark2haru" for configuration "RelWithDebInfo"
set_property(TARGET mark2haru::mark2haru APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(mark2haru::mark2haru PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C;CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/mark2haru_core.lib"
  )

list(APPEND _cmake_import_check_targets mark2haru::mark2haru )
list(APPEND _cmake_import_check_files_for_mark2haru::mark2haru "${_IMPORT_PREFIX}/lib/mark2haru_core.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
