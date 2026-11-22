cmake_minimum_required(VERSION 3.31)

project(Comprendo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(COMPRENDO_BUILD_VIEWER "Build the raylib viewer" ON)
option(COMPRENDO_BUILD_AUDIO "Build SFML audio support" OFF)
option(COMPRENDO_BUILD_PHYSICS "Build Jolt physics support" OFF)
option(COMPRENDO_BUILD_TESTS "Build the Catch2 test suite" ON)
option(COMPRENDO_PROFILER_ENABLED "Compile profiling scopes into the binaries" ON)

include(cmake/git_hash.cmake)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
configure_file(
  cmake/Version.hpp.in
  ${CMAKE_BINARY_DIR}/generated/Version.hpp
  @ONLY
)

file(READ ${CMAKE_SOURCE_DIR}/schema/chart.sql CHART_SCHEMA_SQL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/schema/chart.sql)
configure_file(
  cmake/ChartSchema.hpp.in
  ${CMAKE_BINARY_DIR}/generated/ChartSchema.hpp
  @ONLY
)

include(cmake/dependencies.cmake)
include(cmake/doxygen_setup.cmake)

add_library(
  comprendo_core
  STATIC
  src/Chart.cpp
  src/ChartStore.cpp
  src/CtagsExtractor.cpp
  src/ExtractConfig.cpp
  src/Files.cpp
  src/Includes.cpp
  src/Json.cpp
  src/Process.cpp
  src/Sqlite.cpp
)
target_include_directories(
  comprendo_core
  PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(
  comprendo_core
  PUBLIC fmt::fmt spdlog::spdlog
  PRIVATE comprendo_sqlite3 toml11::toml11 libenvpp::libenvpp
)
target_compile_definitions(
  comprendo_core
  PUBLIC COMPRENDO_PROFILER_ENABLED=$<BOOL:${COMPRENDO_PROFILER_ENABLED}>
)

add_executable(comprendo-cli src/cli_main.cpp)
target_link_libraries(comprendo-cli PRIVATE comprendo_core)

if(COMPRENDO_BUILD_VIEWER)
  add_executable(comprendo src/viewer_main.cpp src/Ui.cpp)
  target_link_libraries(comprendo PRIVATE comprendo_core raylib rlimgui)
endif()

if(COMPRENDO_BUILD_TESTS)
  enable_testing()
  add_executable(
    comprendo_tests
    tests/test_smoke.cpp
    tests/test_chart.cpp
    tests/test_chart_store.cpp
    tests/test_ctags_extractor.cpp
    tests/test_extract_config.cpp
    tests/test_files.cpp
    tests/test_includes.cpp
    tests/test_json.cpp
    tests/test_process.cpp
  )
  target_include_directories(comprendo_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  target_compile_definitions(
    comprendo_tests
    PRIVATE COMPRENDO_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
  )
  target_link_libraries(
    comprendo_tests
    PRIVATE comprendo_core comprendo_sqlite3 Catch2::Catch2WithMain
  )
  catch_discover_tests(comprendo_tests)

  if(COMPRENDO_BUILD_VIEWER)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
      add_test(NAME viewer_smoke COMMAND ${XVFB_RUN} -a $<TARGET_FILE:comprendo> --smoke-test)
    endif()
  endif()
endif()
