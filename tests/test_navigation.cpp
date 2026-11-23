/// \file
/// \brief Tests for the viewer's pure navigation helpers.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
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

/// Double-click and Backspace move the camera in a short eased animation; a
/// centring move must not change the zoom on the way and must end exactly on
/// the target. Animates a pan and a zoom, checks the endpoints, that the move
/// is past halfway at half time (ease-out), that the pan keeps the zoom, that
/// zoom changes geometrically, and that a zero duration jumps.
TEST_CASE("camera animation eases between poses", "[navigation]")
{
  using Comprendo::CameraAnimation;
  using Comprendo::CameraPose;
  const CameraPose from {.target = {.x = 0.0F, .y = 0.0F}, .zoom = 2.0F};
  const CameraPose pan_to {.target = {.x = 10.0F, .y = -20.0F}, .zoom = 2.0F};
  const CameraAnimation pan {from, pan_to, 1.0, 0.25};
  CHECK(pan.at(0.5) == from);
  CHECK(pan.at(1.25) == pan_to);
  CHECK(pan.at(9.0) == pan_to);
  const CameraPose middle = pan.at(1.125);
  CHECK(middle.zoom == 2.0F);
  CHECK(middle.target.x > 5.0F);
  CHECK(middle.target.x < 10.0F);
  CHECK_FALSE(pan.finished(1.2));
  CHECK(pan.finished(1.25));

  const CameraAnimation zoom {from, {.target = {}, .zoom = 8.0F}, 0.0, 1.0};
  const float halfway = zoom.at(1.0 - std::cbrt(0.5)).zoom;
  CHECK(halfway > 3.99F);
  CHECK(halfway < 4.01F);

  CHECK(CameraAnimation {from, pan_to, 0.0, 0.0}.at(0.0) == pan_to);
}

/// Backspace walks back through the views visited before each jump; repeated
/// identical entries would make it seem stuck, and the history must not grow
/// without bound. Pushes views (one twice), pops them in reverse, checks an
/// empty history yields nothing, and checks the capacity drops the oldest.
TEST_CASE("navigation history returns views newest first", "[navigation]")
{
  using Comprendo::NavigationEntry;
  Comprendo::NavigationHistory history {3};
  const NavigationEntry a {.pose = {.target = {.x = 1.0F}, .zoom = 1.0F}, .selected = 7};
  const NavigationEntry b {.pose = {.target = {.x = 2.0F}, .zoom = 1.0F}, .selected = std::nullopt};
  history.push(a);
  history.push(b);
  history.push(b);
  CHECK(history.size() == 2);
  CHECK(history.pop() == std::optional {b});
  CHECK(history.pop() == std::optional {a});
  CHECK_FALSE(history.pop().has_value());

  for (int i = 0; i < 5; ++i)
  {
    history.push({.pose = {.target = {.x = static_cast<float>(i)}, .zoom = 1.0F}});
  }
  CHECK(history.size() == 3);
  const auto newest = history.pop();
  CHECK(newest.has_value());
  if (newest)
  {
    CHECK(newest->pose.target.x == 4.0F);
  }
}
