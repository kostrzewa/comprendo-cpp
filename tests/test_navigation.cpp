/// \file
/// \brief Tests for the viewer's pure navigation helpers.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>

#include "Comprendo/Navigation.hpp"

using Comprendo::move_list_cursor;

/// The Inspector's Up/Down keys walk one cursor through all its lists; it must
/// start at the top, stop at both ends, and vanish when nothing is listed.
/// Moves a cursor from nothing, within, past both ends, and in an empty list,
/// and checks the resulting row.
TEST_CASE("list cursor stays within the list", "[navigation]")
{
  CHECK(move_list_cursor(std::nullopt, 5, 1) == std::optional<std::size_t> {0});
  CHECK(move_list_cursor(std::nullopt, 5, -1) == std::optional<std::size_t> {0});
  CHECK(move_list_cursor(2, 5, 1) == std::optional<std::size_t> {3});
  CHECK(move_list_cursor(2, 5, -1) == std::optional<std::size_t> {1});
  CHECK(move_list_cursor(4, 5, 1) == std::optional<std::size_t> {4});
  CHECK(move_list_cursor(0, 5, -1) == std::optional<std::size_t> {0});
  CHECK(move_list_cursor(9, 5, 0) == std::optional<std::size_t> {4});
  CHECK_FALSE(move_list_cursor(3, 0, 1).has_value());
}
