include(FetchContent)

# ---------------------------------------------------------------------------
# raylib: windowing, 2D rendering, input.
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# SFML: audio only (raylib covers the rest).
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# JoltPhysics: rigid body physics for particle debris.
# ---------------------------------------------------------------------------
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
# HDF5 bindings for structured binary I/O.
# ---------------------------------------------------------------------------
set(HIGHFIVE_USE_BOOST OFF CACHE BOOL "" FORCE)
set(HIGHFIVE_USE_EIGEN OFF CACHE BOOL "" FORCE)
set(HIGHFIVE_USE_XTENSOR OFF CACHE BOOL "" FORCE)
set(HIGHFIVE_USE_OPENCV OFF CACHE BOOL "" FORCE)
set(HIGHFIVE_USE_VTK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  highfive
  GIT_REPOSITORY https://github.com/highfive-devs/highfive.git
  GIT_TAG v3.3.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(highfive)

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
if(QCD_BUILD_TESTS)
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

# ---------------------------------------------------------------------------
# Optional task parallelism. HPX is large and pulls in Boost/hwloc, so it is
# opt-in. The game falls back to std::jthread when it is disabled.
# ---------------------------------------------------------------------------
if(QCD_USE_HPX)
  FetchContent_Declare(
    hpx
    GIT_REPOSITORY https://github.com/STEllAR-GROUP/hpx.git
    GIT_TAG v1.11.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(hpx)
endif()
