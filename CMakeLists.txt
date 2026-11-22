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

option(COMPRENDO_BUILD_DOXYGEN "Build html and latex documentation. Requires a texlive and doxygen installation" OFF)
if(COMPRENDO_BUILD_DOXYGEN)    
  find_package(Doxygen REQUIRED doxygen)    
  include(cmake/doxygen_setup.cmake)
endif()  

add_library(
  comprendo_core
  STATIC
  src/Chart.cpp
  src/ChartStore.cpp
  src/Cli.cpp
  src/CtagsExtractor.cpp
  src/ExtractConfig.cpp
  src/Files.cpp
  src/Hierarchy.cpp
  src/Includes.cpp
  src/Json.cpp
  src/Layout.cpp
  src/Process.cpp
  src/QuadTree.cpp
  src/RadixSort.cpp
  src/Sqlite.cpp
)
target_include_directories(
  comprendo_core
  PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(
  comprendo_core
  PUBLIC fmt::fmt spdlog::spdlog
  PRIVATE comprendo_sqlite3 toml11::toml11 libenvpp::libenvpp CLI11::CLI11
)
target_compile_definitions(
  comprendo_core
  PUBLIC COMPRENDO_PROFILER_ENABLED=$<BOOL:${COMPRENDO_PROFILER_ENABLED}>
)
# Never fuse a*b+c into FMA: fused and unfused rounding differ, which would make
# layouts depend on the target CPU and break golden files across builds.
target_compile_options(comprendo_core PRIVATE -ffp-contract=off)

add_executable(comprendo-cli src/cli_main.cpp)
target_link_libraries(comprendo-cli PRIVATE comprendo_core)

if(COMPRENDO_BUILD_VIEWER)
  add_executable(comprendo src/viewer_main.cpp src/Ui.cpp)
  target_link_libraries(comprendo PRIVATE comprendo_core raylib rlimgui CLI11::CLI11)
endif()

if(COMPRENDO_BUILD_TESTS)
  enable_testing()
  add_executable(
    comprendo_tests
    tests/test_smoke.cpp
    tests/test_chart.cpp
    tests/test_chart_store.cpp
    tests/test_cli.cpp
    tests/test_ctags_extractor.cpp
    tests/test_extract_config.cpp
    tests/test_files.cpp
    tests/test_hierarchy.cpp
    tests/test_includes.cpp
    tests/test_json.cpp
    tests/test_layout.cpp
    tests/test_process.cpp
    tests/test_quadtree.cpp
    tests/test_radix_sort.cpp
    tests/test_random.cpp
  )
  target_include_directories(comprendo_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  target_compile_definitions(
    comprendo_tests
    PRIVATE COMPRENDO_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
            COMPRENDO_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden"
  )
  target_link_libraries(
    comprendo_tests
    PRIVATE comprendo_core comprendo_sqlite3 Catch2::Catch2WithMain
  )
  catch_discover_tests(comprendo_tests)

  add_test(
    NAME cli_validate_rejects_non_chart
    COMMAND comprendo-cli validate ${CMAKE_SOURCE_DIR}/CMakeLists.txt
  )
  set_tests_properties(
    cli_validate_rejects_non_chart
    PROPERTIES PASS_REGULAR_EXPRESSION "invalid chart"
  )

  find_program(UNIVERSAL_CTAGS ctags)
  if(UNIVERSAL_CTAGS)
    set(CLI_TEST_CHART ${CMAKE_BINARY_DIR}/cli-test/chart.sqlite)
    add_test(
      NAME cli_extract
      COMMAND
        comprendo-cli extract --root ${CMAKE_SOURCE_DIR}/tests/fixtures/extract_basic --output
        ${CLI_TEST_CHART}
    )
    add_test(NAME cli_validate COMMAND comprendo-cli validate ${CLI_TEST_CHART})
    add_test(NAME cli_info COMMAND comprendo-cli info ${CLI_TEST_CHART})
    set_tests_properties(cli_extract PROPERTIES FIXTURES_SETUP cli_chart)
    set_tests_properties(cli_validate cli_info PROPERTIES FIXTURES_REQUIRED cli_chart)
    set_tests_properties(cli_validate PROPERTIES PASS_REGULAR_EXPRESSION ": valid")
    set_tests_properties(cli_info PROPERTIES PASS_REGULAR_EXPRESSION "Contains +[0-9]+")
  endif()

  if(COMPRENDO_BUILD_VIEWER)
    add_test(NAME viewer_rejects_unknown_option COMMAND comprendo --no-such-option)
    set_tests_properties(
      viewer_rejects_unknown_option
      PROPERTIES PASS_REGULAR_EXPRESSION "not expected: --no-such-option"
    )
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
      add_test(NAME viewer_smoke COMMAND ${XVFB_RUN} -a $<TARGET_FILE:comprendo> --smoke-test)
    endif()
  endif()
endif()
