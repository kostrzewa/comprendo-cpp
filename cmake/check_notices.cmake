# Check that THIRD_PARTY_NOTICES.md acknowledges every FetchContent dependency.
#
#   cmake -DSOURCE_DIR=<repo> -P cmake/check_notices.cmake
#
# Each FetchContent_Declare(<name> ...) in cmake/*.cmake needs a
# "<!-- fetchcontent: <name> -->" marker above its license text.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "check_notices.cmake: -DSOURCE_DIR=... is required")
endif()

file(READ "${SOURCE_DIR}/THIRD_PARTY_NOTICES.md" notices)
file(GLOB cmake_files "${SOURCE_DIR}/cmake/*.cmake")
set(missing "")
foreach(cmake_file IN LISTS cmake_files)
  file(READ "${cmake_file}" content)
  string(REGEX MATCHALL "FetchContent_Declare\\([ \t\r\n]*[A-Za-z0-9_.+-]+" declarations "${content}")
  foreach(declaration IN LISTS declarations)
    string(REGEX REPLACE "FetchContent_Declare\\([ \t\r\n]*" "" name "${declaration}")
    string(FIND "${notices}" "<!-- fetchcontent: ${name} -->" position)
    if(position EQUAL -1)
      list(APPEND missing "${name}")
    endif()
  endforeach()
endforeach()

if(missing)
  list(JOIN missing ", " missing_text)
  message(FATAL_ERROR "THIRD_PARTY_NOTICES.md does not acknowledge: ${missing_text}")
endif()
