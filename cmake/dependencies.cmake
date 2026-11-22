include(FetchContent)

# ---------------------------------------------------------------------------
# raylib: windowing, 2D rendering, input.
# ---------------------------------------------------------------------------
if(COMPRENDO_BUILD_VIEWER)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(PLATFORM Desktop CACHE STRING "" FORCE)
  FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(raylib)

  # Dear ImGui (docking branch) and the rlImGui raylib backend.
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.7-docking
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(imgui)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
  )
  target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
  )

  FetchContent_Declare(
    rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG Raylib_5_5
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(rlImGui)

  add_library(rlimgui STATIC ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
  target_include_directories(rlimgui PUBLIC ${rlimgui_SOURCE_DIR})
  target_link_libraries(rlimgui PUBLIC imgui raylib)
endif()

# ---------------------------------------------------------------------------
# SFML: audio only (raylib covers the rest).
# ---------------------------------------------------------------------------
if(COMPRENDO_BUILD_AUDIO)
  set(SFML_BUILD_AUDIO ON CACHE BOOL "" FORCE)
  set(SFML_BUILD_GRAPHICS OFF CACHE BOOL "" FORCE)
  set(SFML_BUILD_WINDOW OFF CACHE BOOL "" FORCE)
  set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
  set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.0.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SFML)
endif()

# ---------------------------------------------------------------------------
# JoltPhysics: rigid body physics for particle debris.
# ---------------------------------------------------------------------------
if(COMPRENDO_BUILD_PHYSICS)
  set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
  set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
  set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
  set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
  set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
  set(JPH_ENABLE_ASSERTS ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.5.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(JoltPhysics)
  # Jolt keeps its CMakeLists in Build/, so FetchContent cannot add it directly.
  if(NOT TARGET Jolt)
    add_subdirectory("${joltphysics_SOURCE_DIR}/Build" "${joltphysics_BINARY_DIR}")
  endif()
endif()

# ---------------------------------------------------------------------------
# Formatting and logging.
# ---------------------------------------------------------------------------
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 11.2.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fmt)

set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

# ---------------------------------------------------------------------------
# Configuration and environment.
# ---------------------------------------------------------------------------
FetchContent_Declare(
  libenvpp
  GIT_REPOSITORY https://github.com/ph3at/libenvpp.git
  GIT_TAG v1.5.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libenvpp)

FetchContent_Declare(
  toml11
  GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
  GIT_TAG v4.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(toml11)

# ---------------------------------------------------------------------------
# SQLite amalgamation: the chart store shared with the opencode plugin.
# ---------------------------------------------------------------------------
FetchContent_Declare(
  sqlite3
  URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
  URL_HASH SHA3_256=f131b68e6ba5fb891cc13ebb5ff9555054c77294cb92d8d1268bad5dba4fa2a1
)
FetchContent_MakeAvailable(sqlite3)

find_package(Threads REQUIRED)
add_library(comprendo_sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(comprendo_sqlite3 SYSTEM PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_definitions(
  comprendo_sqlite3
  PRIVATE SQLITE_DQS=0 SQLITE_DEFAULT_FOREIGN_KEYS=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=1
)
target_compile_options(comprendo_sqlite3 PRIVATE -w)
target_link_libraries(comprendo_sqlite3 PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
if(COMPRENDO_BUILD_TESTS)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
  include(Catch)
endif()

