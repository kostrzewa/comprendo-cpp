#pragma once

/// \file
/// \brief Per-user viewer preferences (font, text size, panels) and the fonts to choose from.

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace Comprendo
{
  /// A font the viewer offers.
  struct FontChoice
  {
    /// Stable id stored in the settings file.
    std::string_view id;
    /// Name shown in the Fonts dialog.
    std::string_view name;
    /// TrueType file in the viewer's font directory; empty for the built-in font.
    std::string_view file;
    /// Pixel size at the default text size, chosen so the fonts look alike in size.
    float size = 0.0F;
  };

  /// Id of the built-in font (ImGui's Proggy), the default.
  inline constexpr std::string_view kBuiltinFontId = "proggy";

  /// The fonts the viewer offers, the built-in font first.
  ///
  /// \return Font choices in the order the Fonts dialog lists them.
  [[nodiscard]]
  std::span<const FontChoice>
  font_choices();

  /// Look up a font by id.
  ///
  /// \param id Font id.
  /// \return The font, or null for an unknown id.
  [[nodiscard]]
  const FontChoice *
  find_font(std::string_view id);

  /// Size of a font relative to the built-in one at the same text size level,
  /// so switching fonts keeps text about equally large.
  ///
  /// \param id Font id.
  /// \return The font's size over the built-in font's; 1 for unknown ids.
  [[nodiscard]]
  float
  font_size_factor(std::string_view id);

  /// Viewer preferences that persist across sessions.
  struct ViewerSettings
  {
    /// Font id (see font_choices()).
    std::string font {kBuiltinFontId};
    /// Text size level (see text_scale()); 0 is the default size.
    int text_level = 0;
    /// Whether the Explorer panel is shown.
    bool show_explorer = true;
    /// Whether the Inspector panel is shown.
    bool show_inspector = true;
    /// Whether the Log panel is shown; hidden by default, it is mostly for diagnostics.
    bool show_log = false;
    /// Whether the minimap is drawn in the graph's corner.
    bool show_minimap = true;

    /// Memberwise equality.
    ///
    /// \return True when all members are equal.
    bool
    operator==(const ViewerSettings &) const = default;
  };

  /// Parse a settings file.
  ///
  /// Unknown keys are ignored so newer settings files stay readable, missing
  /// keys keep their defaults, and the text level is clamped to the supported
  /// range.
  ///
  /// \param toml_text File contents (TOML).
  /// \param source_name File name for messages.
  /// \return The settings, or a message for an invalid file or unknown font.
  [[nodiscard]]
  std::expected<ViewerSettings, std::string>
  parse_viewer_settings(std::string_view toml_text, std::string_view source_name);

  /// Write settings as TOML.
  ///
  /// \param settings Settings.
  /// \return File contents that parse_viewer_settings() reads back unchanged.
  [[nodiscard]]
  std::string
  format_viewer_settings(const ViewerSettings &settings);

  /// Where the settings file lives: `$XDG_CONFIG_HOME/comprendo/viewer.toml`,
  /// else `$HOME/.config/comprendo/viewer.toml`.
  ///
  /// \param xdg_config_home Value of `XDG_CONFIG_HOME`, possibly empty.
  /// \param home Value of `HOME`, possibly empty.
  /// \return The path, or empty when neither variable is set.
  [[nodiscard]]
  std::filesystem::path
  viewer_settings_path(std::string_view xdg_config_home, std::string_view home);

  /// viewer_settings_path() for the current environment.
  ///
  /// \return The path, or empty when neither variable is set.
  [[nodiscard]]
  std::filesystem::path
  viewer_settings_path();

  /// Read settings; a missing file gives the defaults.
  ///
  /// \param path Settings file.
  /// \return The settings, or a message when the file exists but is invalid.
  [[nodiscard]]
  std::expected<ViewerSettings, std::string>
  load_viewer_settings(const std::filesystem::path &path);

  /// Write settings, creating the directory; the file is replaced atomically.
  ///
  /// \param path Settings file.
  /// \param settings Settings.
  /// \return Nothing, or a message when the file cannot be written.
  [[nodiscard]]
  std::expected<void, std::string>
  save_viewer_settings(const std::filesystem::path &path, const ViewerSettings &settings);
} // namespace Comprendo
