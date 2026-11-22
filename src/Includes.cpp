/// \file
/// \brief Finding `#include` directives in C-family sources and resolving them to files.

#include "Comprendo/Includes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{
  /// Remove leading spaces and tabs.
  ///
  /// \param text Text to trim.
  /// \return \p text without leading horizontal whitespace.
  std::string_view
  trim_left(std::string_view text)
  {
    const std::size_t first = text.find_first_not_of(" \t");
    return first == std::string_view::npos ? std::string_view {} : text.substr(first);
  }

  /// Parse one line as an include directive.
  ///
  /// \param line Line text without the newline.
  /// \param number 1-based line number.
  /// \return The directive, or nothing when the line is not a literal include.
  std::optional<Comprendo::IncludeDirective>
  parse_directive(std::string_view line, std::uint32_t number)
  {
    constexpr std::string_view kInclude = "include";
    line = trim_left(line);
    if (!line.starts_with('#'))
    {
      return std::nullopt;
    }
    line = trim_left(line.substr(1));
    if (!line.starts_with(kInclude))
    {
      return std::nullopt;
    }
    line = trim_left(line.substr(kInclude.size()));
    if (line.empty() || (line.front() != '"' && line.front() != '<'))
    {
      return std::nullopt;
    }
    const bool angled = line.front() == '<';
    const std::size_t close = line.find(angled ? '>' : '"', 1);
    if (close == std::string_view::npos || close == 1)
    {
      return std::nullopt;
    }
    return Comprendo::IncludeDirective {
      .line = number, .target = std::string {line.substr(1, close - 1)}, .angled = angled
    };
  }

  /// Blank out block comments so directives inside them are not seen.
  ///
  /// Line comments need no handling because a directive must start its line.
  /// String literals containing comment markers are rare in include regions and
  /// are not special-cased.
  ///
  /// \param source File contents.
  /// \return Copy of \p source with block-comment bodies replaced by spaces,
  ///         newlines preserved.
  std::string
  strip_block_comments(std::string_view source)
  {
    std::string out {source};
    bool in_comment = false;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
      if (!in_comment && out[i] == '/' && i + 1 < out.size() && out[i + 1] == '*')
      {
        in_comment = true;
        out[i] = ' ';
        out[++i] = ' ';
      }
      else if (in_comment && out[i] == '*' && i + 1 < out.size() && out[i + 1] == '/')
      {
        in_comment = false;
        out[i] = ' ';
        out[++i] = ' ';
      }
      else if (in_comment && out[i] != '\n')
      {
        out[i] = ' ';
      }
      else if (!in_comment && out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/')
      {
        while (i < out.size() && out[i] != '\n')
        {
          ++i;
        }
      }
    }
    return out;
  }

  /// Join a directory and a relative path lexically.
  ///
  /// \param directory Repository-relative directory, "" or "." for the root.
  /// \param target Relative path.
  /// \return Normalized '/'-separated path, or nothing when it escapes the root.
  std::optional<std::string>
  join(std::string_view directory, std::string_view target)
  {
    const std::filesystem::path joined
      = (std::filesystem::path {directory} / target).lexically_normal();
    std::string text = joined.generic_string();
    if (text.starts_with("../") || text == ".." || joined.is_absolute())
    {
      return std::nullopt;
    }
    return text;
  }
} // namespace

namespace Comprendo
{
  std::vector<IncludeDirective>
  scan_includes(std::string_view source)
  {
    const std::string text = strip_block_comments(source);
    std::vector<IncludeDirective> directives;
    std::uint32_t number = 0;
    std::size_t start = 0;
    while (start <= text.size())
    {
      ++number;
      std::size_t end = text.find('\n', start);
      if (end == std::string::npos)
      {
        end = text.size();
      }
      if (
        auto directive = parse_directive(std::string_view {text}.substr(start, end - start), number)
      )
      {
        directives.push_back(std::move(*directive));
      }
      start = end + 1;
    }
    return directives;
  }

  std::optional<std::string>
  resolve_include(
    std::string_view includer,
    const IncludeDirective &directive,
    const std::unordered_set<std::string> &known_files,
    const std::vector<std::string> &include_paths
  )
  {
    const auto known = [&known_files](const std::optional<std::string> &path)
    { return path && known_files.contains(*path); };

    if (!directive.angled)
    {
      const std::string directory = std::filesystem::path {includer}.parent_path().generic_string();
      if (auto local = join(directory, directive.target); known(local))
      {
        return local;
      }
    }
    for (const std::string &include_path : include_paths)
    {
      if (auto candidate = join(include_path, directive.target); known(candidate))
      {
        return candidate;
      }
    }
    return std::nullopt;
  }
} // namespace Comprendo
