/// \file
/// \brief Tests for the per-user viewer settings and the font catalogue.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <string_view>

#include "Comprendo/Navigation.hpp"
#include "Comprendo/ViewerSettings.hpp"
#include "TempDir.hpp"

using Comprendo::ViewerSettings;

/// The settings file is written by the viewer and may be edited by hand, so
/// what the viewer writes must read back unchanged and hand edits must be
/// checked. Round-trips non-default font, size, and panel settings, reads an
/// empty file as the defaults (Log hidden), clamps an out-of-range level,
/// ignores unknown keys, and rejects an unknown font, wrongly typed values,
/// and malformed TOML.
TEST_CASE("viewer settings parse, format, and validate", "[settings]")
{
  using Comprendo::parse_viewer_settings;
  const ViewerSettings custom {
    .font = "inter",
    .text_level = 3,
    .show_explorer = false,
    .show_inspector = true,
    .show_log = true,
    .show_minimap = false,
    .show_source = true,
    .folded_left = {"Explorer", "Inspector"},
    .folded_right = {"Log"},
  };
  CHECK(parse_viewer_settings(Comprendo::format_viewer_settings(custom), "t") == custom);
  CHECK(parse_viewer_settings("", "t") == ViewerSettings {});
  CHECK_FALSE(ViewerSettings {}.show_log);
  CHECK_FALSE(ViewerSettings {}.show_source);
  CHECK(parse_viewer_settings("show_log = true\n", "t") == ViewerSettings {.show_log = true});
  CHECK_FALSE(parse_viewer_settings("show_log = 1\n", "t").has_value());
  CHECK_FALSE(parse_viewer_settings("folded_left = \"Explorer\"\n", "t").has_value());
  CHECK_FALSE(parse_viewer_settings("folded_left = [1]\n", "t").has_value());
  CHECK(
    parse_viewer_settings("text_level = 99\n", "t")
    == ViewerSettings {.text_level = Comprendo::kMaxTextLevel}
  );
  CHECK(
    parse_viewer_settings("font = \"jetbrains-mono\"\ntheme = \"dark\"\n", "t")
    == ViewerSettings {.font = "jetbrains-mono"}
  );
  const auto unknown = parse_viewer_settings("font = \"comic\"\n", "viewer.toml");
  CHECK_FALSE(unknown.has_value());
  CHECK(unknown.error_or("").find("unknown font") != std::string::npos);
  CHECK_FALSE(parse_viewer_settings("text_level = \"big\"\n", "t").has_value());
  CHECK_FALSE(parse_viewer_settings("font = \n", "t").has_value());
}

/// Preferences belong to the user, not the project, and follow the XDG base
/// directory convention. Checks XDG_CONFIG_HOME wins over HOME, HOME falls
/// back to ~/.config, and nothing is returned without either, so the viewer
/// then keeps settings for the session only.
TEST_CASE("viewer settings live in the user's configuration directory", "[settings]")
{
  using Comprendo::viewer_settings_path;
  CHECK(viewer_settings_path("/x/config", "/home/u") == "/x/config/comprendo/viewer.toml");
  CHECK(viewer_settings_path("", "/home/u") == "/home/u/.config/comprendo/viewer.toml");
  CHECK(viewer_settings_path("", "").empty());
}

/// The viewer saves on every change, so saving must create the directory and
/// replace the file whole, and a missing file must mean the defaults. Loads
/// from an empty directory, saves twice into a new subdirectory, reads the
/// second value back, and checks no temporary file is left behind.
TEST_CASE("viewer settings save and load through a file", "[settings]")
{
  const Comprendo::Testing::TempDir dir {"viewer-settings"};
  const std::filesystem::path file = dir / "config/comprendo/viewer.toml";
  CHECK(Comprendo::load_viewer_settings(file) == ViewerSettings {});
  CHECK(Comprendo::save_viewer_settings(file, {.font = "inter", .text_level = 1}).has_value());
  const ViewerSettings second {.font = "atkinson-hyperlegible", .text_level = -2};
  CHECK(Comprendo::save_viewer_settings(file, second).has_value());
  CHECK(Comprendo::load_viewer_settings(file) == second);
  CHECK_FALSE(std::filesystem::exists(dir / "config/comprendo/viewer.toml.tmp"));
}

/// The Fonts dialog lists the catalogue and the settings file stores ids, so
/// ids and files must be unique, the built-in font must come first and need
/// no file, and the size factor must keep the built-in font at 1. Checks the
/// catalogue and find_font for known and unknown ids.
TEST_CASE("font catalogue", "[settings]")
{
  const auto fonts = Comprendo::font_choices();
  CHECK(fonts.size() >= 2);
  CHECK(fonts.front().id == Comprendo::kBuiltinFontId);
  CHECK(fonts.front().file.empty());
  std::set<std::string_view> ids;
  std::set<std::string_view> files;
  for (const Comprendo::FontChoice &font : fonts)
  {
    ids.insert(font.id);
    files.insert(font.file);
    CHECK(font.size > 0.0F);
  }
  CHECK(ids.size() == fonts.size());
  CHECK(files.size() == fonts.size());
  CHECK(Comprendo::find_font("inter") != nullptr);
  CHECK(Comprendo::find_font("comic") == nullptr);
  CHECK(Comprendo::font_size_factor(Comprendo::kBuiltinFontId) == 1.0F);
  CHECK(Comprendo::font_size_factor("comic") == 1.0F);
}
