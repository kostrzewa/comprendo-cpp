#pragma once

/// \file
/// \brief Extractor configuration from defaults, `.comprendo/config.toml`, and the environment.

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Comprendo
{
  /// Settings controlling extraction from a source tree.
  struct ExtractConfig
  {
    /// Project root; all other paths are relative to it.
    std::filesystem::path root;
    /// Subdirectories to scan ("." for the whole tree).
    std::vector<std::string> roots = {"."};
    /// Glob patterns of paths to skip (see glob_match()).
    std::vector<std::string> excludes = {".git/**", ".comprendo/**", "build/**"};
    /// ctags languages to extract; files in other languages are ignored. ctags
    /// treats `.h` as C++, so the default covers C++ headers and sources.
    std::vector<std::string> languages = {"C++"};
    /// Directories searched for `#include` targets, relative to the root.
    std::vector<std::string> include_paths = {"include", "src", "."};
    /// Universal Ctags executable.
    std::string ctags = "ctags";
    /// Number of files passed to one ctags invocation.
    std::size_t ctags_batch = 256;
  };

  /// Apply a `config.toml` document on top of the default configuration.
  ///
  /// Recognized keys live in the `[extract]` table: `roots`, `excludes`,
  /// `languages`, `include_paths` (string arrays), `ctags` (string), and
  /// `ctags_batch` (positive integer). Unknown keys are errors.
  ///
  /// \param config Configuration to update.
  /// \param toml_text TOML document.
  /// \param source_name Name used in error messages, usually the file path.
  /// \return Nothing, or an error message naming the offending key.
  [[nodiscard]]
  std::expected<void, std::string>
  apply_config_toml(
    ExtractConfig &config, std::string_view toml_text, std::string_view source_name
  );

  /// Apply environment overrides: `COMPRENDO_CTAGS` replaces the ctags executable.
  ///
  /// \param config Configuration to update.
  /// \param environment Environment to read instead of the process environment,
  ///        for tests; `std::nullopt` reads the real environment.
  /// \return Nothing, or an error message.
  [[nodiscard]]
  std::expected<void, std::string>
  apply_environment(
    ExtractConfig &config,
    const std::optional<std::unordered_map<std::string, std::string>> &environment = std::nullopt
  );

  /// Build the configuration for a project root.
  ///
  /// Starts from the defaults, applies `<root>/.comprendo/config.toml` when it
  /// exists, then the environment.
  ///
  /// \param root Project root.
  /// \return The configuration, or an error message.
  [[nodiscard]]
  std::expected<ExtractConfig, std::string>
  load_extract_config(const std::filesystem::path &root);
} // namespace Comprendo
