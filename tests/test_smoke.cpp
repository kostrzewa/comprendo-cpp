/// \file
/// \brief Smoke tests for the M0 skeleton.

#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "Version.hpp"

/// The startup log must contain the project version and git commit, both injected
/// by CMake into the generated Version.hpp. Checks both macros expand to non-empty
/// strings, which fails if the configure step stops generating them.
TEST_CASE("version header is populated", "[smoke]")
{
  CHECK(std::strlen(COMPRENDO_VERSION) > 0);
  CHECK(std::strlen(COMPRENDO_GIT_COMMIT) > 0);
}
