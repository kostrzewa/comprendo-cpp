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

find_program(COMPRENDO_RUN_CLANG_TIDY run-clang-tidy-22)
find_program(COMPRENDO_CLANG_TIDY clang-tidy-22)
if(COMPRENDO_RUN_CLANG_TIDY AND COMPRENDO_CLANG_TIDY)
  add_custom_target(
    clang-tidy
    COMMAND
      ${COMPRENDO_RUN_CLANG_TIDY} -clang-tidy-binary ${COMPRENDO_CLANG_TIDY} -p ${CMAKE_BINARY_DIR}
      -quiet "${CMAKE_SOURCE_DIR}/(src|tests)/.*\\.cpp$"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy-22 on src/ and tests/"
    USES_TERMINAL
    VERBATIM
  )
endif()

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
  src/ChartTools.cpp
  src/ChartView.cpp
  src/ChartWatcher.cpp
  src/ClangdExtractor.cpp
  src/Cli.cpp
  src/CtagsExtractor.cpp
  src/DocComments.cpp
  src/ExtractConfig.cpp
  src/Files.cpp
  src/Hierarchy.cpp
  src/Includes.cpp
  src/Json.cpp
  src/Lsp.cpp
  src/Mcp.cpp
  src/Navigation.cpp
  src/Layout.cpp
  src/Process.cpp
  src/QuadTree.cpp
  src/RadixSort.cpp
  src/Threads.cpp
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
  add_executable(comprendo src/viewer_main.cpp src/Ui.cpp src/GraphRenderer.cpp)
  target_link_libraries(comprendo PRIVATE comprendo_core raylib rlimgui CLI11::CLI11)
endif()

if(COMPRENDO_BUILD_TESTS)
  enable_testing()
  add_executable(
    comprendo_tests
    tests/test_smoke.cpp
    tests/test_threads.cpp
    tests/test_chart.cpp
    tests/test_chart_store.cpp
    tests/test_chart_tools.cpp
    tests/test_chart_view.cpp
    tests/test_chart_watcher.cpp
    tests/test_clangd_extractor.cpp
    tests/test_cli.cpp
    tests/test_doc_comments.cpp
    tests/test_ctags_extractor.cpp
    tests/test_extract_config.cpp
    tests/test_files.cpp
    tests/test_hierarchy.cpp
    tests/test_includes.cpp
    tests/test_json.cpp
    tests/test_layout.cpp
    tests/test_lsp.cpp
    tests/test_mcp.cpp
    tests/test_navigation.cpp
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
        comprendo-cli extract --backend ctags --root ${CMAKE_SOURCE_DIR}/tests/fixtures/extract_basic
        --output
        ${CLI_TEST_CHART}
    )
    add_test(
      NAME cli_profile
      COMMAND
        comprendo-cli --profile ${CMAKE_BINARY_DIR}/cli-test/extract.trace.json extract --backend
        ctags --root ${CMAKE_SOURCE_DIR}/tests/fixtures/extract_basic --output
        ${CMAKE_BINARY_DIR}/cli-test/profiled.sqlite
    )
    set_tests_properties(cli_profile PROPERTIES PASS_REGULAR_EXPRESSION "Trace written to")
    add_test(NAME cli_validate COMMAND comprendo-cli validate ${CLI_TEST_CHART})
    add_test(NAME cli_info COMMAND comprendo-cli info ${CLI_TEST_CHART})
    set_tests_properties(cli_extract PROPERTIES FIXTURES_SETUP cli_chart)
    set_tests_properties(cli_validate cli_info PROPERTIES FIXTURES_REQUIRED cli_chart)
    set_tests_properties(cli_validate PROPERTIES PASS_REGULAR_EXPRESSION ": valid")
    set_tests_properties(cli_info PROPERTIES PASS_REGULAR_EXPRESSION "Contains +[0-9]+")
  endif()

  # Every fetched dependency must be acknowledged with its license text.
  add_test(
    NAME third_party_notices
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/check_notices.cmake
  )

  # The /chart skill of the Claude Code plugin carries a copy of the shared prompt.
  add_test(
    NAME chart_prompt_in_sync
    COMMAND
      ${CMAKE_COMMAND} -DMODE=check -DPROMPT=${CMAKE_SOURCE_DIR}/prompts/chart.md
      -DSKILL=${CMAKE_SOURCE_DIR}/claude_plugin/skills/chart/SKILL.md -P
      ${CMAKE_SOURCE_DIR}/cmake/sync_prompt.cmake
  )

  # A recorded /chart session through `comprendo-cli mcp`, on the extracted sample chart.
  if(UNIVERSAL_CTAGS)
    set(MCP_TEST_DIR ${CMAKE_BINARY_DIR}/mcp-test)
    set(MCP_TEST_CHART ${MCP_TEST_DIR}/.comprendo/chart.sqlite)
    add_test(
      NAME mcp_annotate
      COMMAND
        ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:comprendo-cli> -DSOURCE=${CLI_TEST_CHART}
        -DPROJECT=${MCP_TEST_DIR} -DSESSION=${CMAKE_SOURCE_DIR}/tests/fixtures/mcp/annotate.jsonl
        -P ${CMAKE_SOURCE_DIR}/tests/run_mcp_session.cmake
    )
    add_test(NAME mcp_chart_validate COMMAND comprendo-cli validate ${MCP_TEST_CHART})
    add_test(NAME mcp_chart_info COMMAND comprendo-cli info ${MCP_TEST_CHART})
    set_tests_properties(
      mcp_annotate
      PROPERTIES FIXTURES_REQUIRED cli_chart FIXTURES_SETUP mcp_chart
    )
    set_tests_properties(mcp_chart_validate mcp_chart_info PROPERTIES FIXTURES_REQUIRED mcp_chart)
    set_tests_properties(mcp_chart_validate PROPERTIES PASS_REGULAR_EXPRESSION ": valid")
    set_tests_properties(mcp_chart_info PROPERTIES PASS_REGULAR_EXPRESSION "Uses +1")
  endif()

  # The opencode plugins run on Bun; opencode is a Bun executable and acts as
  # `bun` with BUN_BE_BUN=1.
  find_program(COMPRENDO_BUN bun)
  find_program(COMPRENDO_OPENCODE opencode)
  if(COMPRENDO_BUN)
    set(COMPRENDO_BUN_COMMAND ${COMPRENDO_BUN})
  elseif(COMPRENDO_OPENCODE)
    set(COMPRENDO_BUN_COMMAND ${CMAKE_COMMAND} -E env BUN_BE_BUN=1 ${COMPRENDO_OPENCODE})
  endif()
  if(COMPRENDO_BUN_COMMAND)
    add_test(
      NAME plugin_tests
      COMMAND ${COMPRENDO_BUN_COMMAND} test
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/plugin
    )
    add_test(
      NAME plugin_v2_tests
      COMMAND ${COMPRENDO_BUN_COMMAND} test
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/plugin_v2
    )
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
      if(UNIVERSAL_CTAGS)
        add_test(
          NAME viewer_smoke_chart
          COMMAND ${XVFB_RUN} -a $<TARGET_FILE:comprendo> --smoke-test ${CLI_TEST_CHART}
        )
        set_tests_properties(viewer_smoke_chart PROPERTIES FIXTURES_REQUIRED cli_chart)
        add_test(
          NAME viewer_smoke_mcp_chart
          COMMAND ${XVFB_RUN} -a $<TARGET_FILE:comprendo> --smoke-test ${MCP_TEST_CHART}
        )
        set_tests_properties(viewer_smoke_mcp_chart PROPERTIES FIXTURES_REQUIRED mcp_chart)
      endif()
    endif()
  endif()
endif()
