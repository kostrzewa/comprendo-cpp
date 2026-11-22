/// \file
/// \brief Extractor configuration from defaults, `.comprendo/config.toml`, and the environment.

#include "Comprendo/ExtractConfig.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <libenvpp/env.hpp>
#include <toml.hpp>

#include "Logger/Logger.hpp"

namespace
{
  /// Read a TOML array of strings.
  ///
  /// \param value TOML value.
  /// \param key Key name for error messages.
  /// \return The strings, or an error message.
  std::expected<std::vector<std::string>, std::string>
  string_array(const toml::value &value, std::string_view key)
  {
    if (!value.is_array())
    {
      return std::unexpected(fmt::format("extract.{} must be an array of strings", key));
    }
    std::vector<std::string> strings;
    for (const toml::value &element : value.as_array())
    {
      if (!element.is_string())
      {
        return std::unexpected(fmt::format("extract.{} must contain only strings", key));
      }
      strings.push_back(element.as_string());
    }
    return strings;
  }

  /// Apply one `[extract]` key.
  ///
  /// \param config Configuration to update.
  /// \param key Key name.
  /// \param value Key value.
  /// \return Nothing, or an error message.
  std::expected<void, std::string>
  apply_extract_key(
    Comprendo::ExtractConfig &config, const std::string &key, const toml::value &value
  )
  {
    const std::unordered_map<std::string_view, std::vector<std::string> *> arrays = {
      {"roots", &config.roots},
      {"excludes", &config.excludes},
      {"languages", &config.languages},
      {"include_paths", &config.include_paths},
    };
    if (const auto array = arrays.find(key); array != arrays.end())
    {
      return string_array(value, key)
        .transform([&](std::vector<std::string> strings) { *array->second = std::move(strings); });
    }
    if (key == "ctags")
    {
      if (!value.is_string() || value.as_string().empty())
      {
        return std::unexpected("extract.ctags must be a non-empty string");
      }
      config.ctags = value.as_string();
      return {};
    }
    if (key == "ctags_batch")
    {
      if (!value.is_integer() || value.as_integer() <= 0)
      {
        return std::unexpected("extract.ctags_batch must be a positive integer");
      }
      config.ctags_batch = static_cast<std::size_t>(value.as_integer());
      return {};
    }
    return std::unexpected(fmt::format("unknown key extract.{}", key));
  }
} // namespace

namespace Comprendo
{
  std::expected<void, std::string>
  apply_config_toml(ExtractConfig &config, std::string_view toml_text, std::string_view source_name)
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
    for (const auto &[section, body] : document.as_table())
    {
      if (section != "extract")
      {
        return std::unexpected(fmt::format("{}: unknown section [{}]", source_name, section));
      }
      if (!body.is_table())
      {
        return std::unexpected(fmt::format("{}: extract must be a table", source_name));
      }
      for (const auto &[key, value] : body.as_table())
      {
        if (auto applied = apply_extract_key(config, key, value); !applied)
        {
          return std::unexpected(fmt::format("{}: {}", source_name, applied.error()));
        }
      }
    }
    return {};
  }

  std::expected<void, std::string>
  apply_environment(
    ExtractConfig &config,
    const std::optional<std::unordered_map<std::string, std::string>> &environment
  )
  {
    auto prefix = env::prefix("COMPRENDO");
    const auto ctags_id = prefix.register_variable<std::string>("CTAGS");
    const auto parsed
      = environment ? prefix.parse_and_validate(*environment) : prefix.parse_and_validate();
    if (!parsed.ok())
    {
      return std::unexpected(parsed.error_message());
    }
    if (const auto ctags = parsed.get(ctags_id); ctags && !ctags->empty())
    {
      config.ctags = *ctags;
    }
    return {};
  }

  std::expected<ExtractConfig, std::string>
  load_extract_config(const std::filesystem::path &root)
  {
    ExtractConfig config;
    config.root = root;

    const std::filesystem::path file = root / ".comprendo" / "config.toml";
    if (std::filesystem::is_regular_file(file))
    {
      std::ifstream stream {file, std::ios::binary};
      const std::string text {std::istreambuf_iterator<char> {stream}, {}};
      if (auto applied = apply_config_toml(config, text, file.string()); !applied)
      {
        return std::unexpected(applied.error());
      }
      Logger::debug("loaded extractor config {}", file.string());
    }

    return apply_environment(config).transform([&config] { return std::move(config); });
  }
} // namespace Comprendo
