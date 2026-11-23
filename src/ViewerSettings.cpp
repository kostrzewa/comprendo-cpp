/// \file
/// \brief Per-user viewer preferences: font catalogue, TOML parsing, and the settings file.

#include "Comprendo/ViewerSettings.hpp"

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>
#include <libenvpp/env.hpp>
#include <toml.hpp>

#include "Comprendo/Navigation.hpp"

namespace
{
  /// The fonts offered by the viewer; files are fetched at build time (see
  /// cmake/dependencies.cmake) and listed in THIRD_PARTY_NOTICES.md.
  constexpr std::array<Comprendo::FontChoice, 4> kFonts = {{
    {.id = Comprendo::kBuiltinFontId, .name = "Proggy (built-in)", .file = "", .size = 13.0F},
    {.id = "atkinson-hyperlegible",
     .name = "Atkinson Hyperlegible",
     .file = "AtkinsonHyperlegible-Regular.ttf",
     .size = 15.0F},
    {.id = "inter", .name = "Inter", .file = "Inter-Variable.ttf", .size = 14.0F},
    {.id = "jetbrains-mono",
     .name = "JetBrains Mono",
     .file = "JetBrainsMono-Variable.ttf",
     .size = 14.0F},
  }};

  /// Name of the settings file inside the configuration directory.
  constexpr std::string_view kSettingsFile = "comprendo/viewer.toml";
} // namespace

namespace Comprendo
{
  std::span<const FontChoice>
  font_choices()
  {
    return kFonts;
  }

  const FontChoice *
  find_font(std::string_view id)
  {
    const auto found = std::ranges::find(kFonts, id, &FontChoice::id);
    return found == kFonts.end() ? nullptr : &*found;
  }

  float
  font_size_factor(std::string_view id)
  {
    const FontChoice *font = find_font(id);
    return font == nullptr ? 1.0F : font->size / kFonts.front().size;
  }

  std::expected<ViewerSettings, std::string>
  parse_viewer_settings(std::string_view toml_text, std::string_view source_name)
  {
    std::istringstream stream {std::string {toml_text}};
    auto parsed = toml::try_parse(stream, std::string {source_name});
    if (!parsed.is_ok())
    {
      std::string message;
      for (const toml::error_info &error : parsed.unwrap_err())
      {
        message += toml::format_error(error);
      }
      return std::unexpected(message);
    }
    const toml::value &document = parsed.unwrap();
    ViewerSettings settings;
    if (document.contains("font"))
    {
      const toml::value &font = document.at("font");
      if (!font.is_string() || find_font(font.as_string()) == nullptr)
      {
        return std::unexpected(fmt::format("{}: unknown font {}", source_name, toml::format(font)));
      }
      settings.font = font.as_string();
    }
    if (document.contains("text_level"))
    {
      const toml::value &level = document.at("text_level");
      if (!level.is_integer())
      {
        return std::unexpected(fmt::format("{}: text_level must be an integer", source_name));
      }
      constexpr toml::value::integer_type kLimit = 1000;
      settings.text_level
        = step_text_level(0, static_cast<int>(std::clamp(level.as_integer(), -kLimit, kLimit)));
    }
    return settings;
  }

  std::string
  format_viewer_settings(const ViewerSettings &settings)
  {
    return fmt::format(
      "# Comprendo viewer settings, written by the viewer.\nfont = \"{}\"\ntext_level = {}\n",
      settings.font,
      settings.text_level
    );
  }

  std::filesystem::path
  viewer_settings_path(std::string_view xdg_config_home, std::string_view home)
  {
    if (!xdg_config_home.empty())
    {
      return std::filesystem::path {xdg_config_home} / kSettingsFile;
    }
    if (!home.empty())
    {
      return std::filesystem::path {home} / ".config" / kSettingsFile;
    }
    return {};
  }

  std::filesystem::path
  viewer_settings_path()
  {
    return viewer_settings_path(
      env::get_or<std::string>("XDG_CONFIG_HOME", ""), env::get_or<std::string>("HOME", "")
    );
  }

  std::expected<ViewerSettings, std::string>
  load_viewer_settings(const std::filesystem::path &path)
  {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
      return ViewerSettings {};
    }
    std::ifstream stream {path, std::ios::binary};
    const std::string text {std::istreambuf_iterator<char> {stream}, {}};
    return parse_viewer_settings(text, path.string());
  }

  std::expected<void, std::string>
  save_viewer_settings(const std::filesystem::path &path, const ViewerSettings &settings)
  {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
      return std::unexpected(
        fmt::format("cannot create {}: {}", path.parent_path().string(), error.message())
      );
    }
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
      std::ofstream stream {temp, std::ios::binary | std::ios::trunc};
      stream << format_viewer_settings(settings);
      if (!stream)
      {
        return std::unexpected(fmt::format("cannot write {}", temp.string()));
      }
    }
    std::filesystem::rename(temp, path, error);
    if (error)
    {
      return std::unexpected(fmt::format("cannot replace {}: {}", path.string(), error.message()));
    }
    return {};
  }
} // namespace Comprendo
