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

/// The Inspector's lists share the window height: short lists must not waste
/// space, a long list should take what they leave, and nothing may collapse
/// below a few rows. Shares heights for lists that all fit, for one long and
/// two short lists, for two long lists, and for a too-small window.
TEST_CASE("panel heights share the available space", "[navigation]")
{
  using Comprendo::share_heights;
  const std::vector<float> fit = {50.0F, 100.0F, 30.0F};
  CHECK(share_heights(fit, 500.0F, 40.0F) == fit);
  CHECK(
    share_heights(std::vector {40.0F, 2000.0F, 60.0F}, 600.0F, 40.0F)
    == std::vector {40.0F, 500.0F, 60.0F}
  );
  CHECK(
    share_heights(std::vector {900.0F, 2000.0F}, 600.0F, 40.0F) == std::vector {300.0F, 300.0F}
  );
  CHECK(
    share_heights(std::vector {500.0F, 500.0F, 20.0F}, 50.0F, 40.0F)
    == std::vector {40.0F, 40.0F, 20.0F}
  );
  CHECK(share_heights(std::vector<float> {}, 100.0F, 40.0F).empty());
}

/// Ctrl++ and Ctrl+- step the text size in levels, so pressing each the same
/// number of times must return to exactly the default size, and the size must
/// stay within readable bounds. Steps up and down, checks the clamping at both
/// ends, the scale of a level, and that level 0 is exactly the default.
TEST_CASE("text size levels step, clamp, and scale", "[navigation]")
{
  using Comprendo::kMaxTextLevel;
  using Comprendo::kMinTextLevel;
  using Comprendo::step_text_level;
  using Comprendo::text_scale;
  CHECK(step_text_level(step_text_level(0, 3), -3) == 0);
  CHECK(step_text_level(kMaxTextLevel, 1) == kMaxTextLevel);
  CHECK(step_text_level(kMinTextLevel, -1) == kMinTextLevel);
  CHECK(step_text_level(0, 100) == kMaxTextLevel);
  CHECK(text_scale(0) == 1.0F);
  CHECK(std::abs(text_scale(2) - (Comprendo::kTextScaleStep * Comprendo::kTextScaleStep)) < 1e-6F);
  CHECK(text_scale(kMinTextLevel) > 0.5F);
  CHECK(text_scale(kMaxTextLevel) < 3.0F);
}

/// The shortcuts are matched on the character a key types, so they work on
/// layouts with a separate + key as well as on US layouts, where + is
/// Shift+=. Checks +, =, -, and 0 map to their actions and other characters,
/// including the null character of keys without one, do nothing.
TEST_CASE("text size shortcuts are recognised by character", "[navigation]")
{
  using Comprendo::text_size_key;
  using Comprendo::TextSizeKey;
  CHECK(text_size_key('+') == TextSizeKey::Larger);
  CHECK(text_size_key('=') == TextSizeKey::Larger);
  CHECK(text_size_key('-') == TextSizeKey::Smaller);
  CHECK(text_size_key('0') == TextSizeKey::Reset);
  CHECK(text_size_key(']') == TextSizeKey::None);
  CHECK(text_size_key('\0') == TextSizeKey::None);
}

/// Docked panels fold to their tab bar with the ▼ button and unfold again,
/// so a second click must restore the previous size, and a panel folded in an
/// earlier session must still unfold to something useful without crowding out
/// its neighbour. Folds a panel, unfolds it with and without a remembered
/// size, and checks the result leaves room for the neighbour's tab bar.
TEST_CASE("panels fold to their tab bar and unfold", "[navigation]")
{
  using Comprendo::fold_toggle_size;
  using Comprendo::is_folded;
  constexpr float kTab = 20.0F;
  CHECK(fold_toggle_size(300.0F, kTab, 0.0F, 800.0F) == kTab);
  CHECK(is_folded(kTab, kTab));
  CHECK(is_folded(kTab + 1.0F, kTab));
  CHECK_FALSE(is_folded(kTab + 10.0F, kTab));
  CHECK(fold_toggle_size(kTab, kTab, 300.0F, 800.0F) == 300.0F);
  CHECK(fold_toggle_size(kTab, kTab, 0.0F, 800.0F) == 800.0F * Comprendo::kUnfoldShare);
  CHECK(fold_toggle_size(kTab, kTab, 900.0F, 800.0F) == 800.0F - kTab);
}

/// Folded side panels become strips that keep the panels' arrangement, so
/// the user recognises which strip is which panel. Arranges two panels that
/// were one above the other and two that were side by side, on the left and
/// on the right edge, and checks columns count from the screen edge and rows
/// from the top.
TEST_CASE("strips of folded side panels keep the panels' arrangement", "[navigation]")
{
  using Comprendo::arrange_strips;
  using Comprendo::StripSlot;
  using Comprendo::Vec2;
  const std::vector<Vec2> stacked = {{.x = 0.0F, .y = 400.0F}, {.x = 0.0F, .y = 20.0F}};
  CHECK(arrange_strips(stacked, false) == std::vector<StripSlot> {{0, 1}, {0, 0}});
  const std::vector<Vec2> side_by_side = {{.x = 300.0F, .y = 20.0F}, {.x = 0.0F, .y = 20.0F}};
  CHECK(arrange_strips(side_by_side, false) == std::vector<StripSlot> {{1, 0}, {0, 0}});
  const std::vector<Vec2> right = {{.x = 900.0F, .y = 20.0F}, {.x = 1100.0F, .y = 20.0F}};
  CHECK(arrange_strips(right, true) == std::vector<StripSlot> {{1, 0}, {0, 0}});
  CHECK(arrange_strips(std::vector<Vec2> {}, false).empty());
}
