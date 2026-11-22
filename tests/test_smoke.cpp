/// \file
/// \brief Smoke tests for the M0 skeleton.

#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "Version.hpp"

/// The generated version header must define a non-empty version string.
TEST_CASE("version header is populated", "[smoke]")
{
  REQUIRE(std::strlen(COMPRENDO_VERSION) > 0);
  REQUIRE(std::strlen(COMPRENDO_GIT_COMMIT) > 0);
}
