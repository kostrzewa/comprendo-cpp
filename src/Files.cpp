/// \file
/// \brief Source-file enumeration with `git ls-files` and a directory-walk fallback.

#include "Comprendo/Files.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fmt/format.h>

#include "Comprendo/Process.hpp"
#include "Logger/Logger.hpp"
#include "Profiler/Profiler.hpp"

namespace
{
  /// Split a '/'-separated path into its non-empty segments.
  ///
  /// \param path Path to split.
  /// \return Segments in order; views into \p path.
  std::vector<std::string_view>
  split_segments(std::string_view path)
  {
    std::vector<std::string_view> segments;
    for (const auto segment : std::views::split(path, '/'))
    {
      if (!segment.empty())
      {
        segments.emplace_back(segment.begin(), segment.end());
      }
    }
    return segments;
  }

  /// Match one path segment against a pattern segment with `*` and `?`.
  ///
  /// Greedy matching with a single backtrack point, linear in practice.
  ///
  /// \param pattern Pattern segment.
  /// \param text Path segment.
  /// \return True on a full match.
  bool
  match_segment(std::string_view pattern, std::string_view text)
  {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string_view::npos;
    std::size_t star_text = 0;
    while (t < text.size())
    {
      if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t]))
      {
        ++p;
        ++t;
      }
      else if (p < pattern.size() && pattern[p] == '*')
      {
        star = p++;
        star_text = t;
      }
      else if (star != std::string_view::npos)
      {
        p = star + 1;
        t = ++star_text;
      }
      else
      {
        return false;
      }
    }
    while (p < pattern.size() && pattern[p] == '*')
    {
      ++p;
    }
    return p == pattern.size();
  }

  /// Match path segments against pattern segments, where `**` spans segments.
  ///
  /// \param pattern Remaining pattern segments.
  /// \param path Remaining path segments.
  /// \return True on a full match.
  bool
  match_segments(std::span<const std::string_view> pattern, std::span<const std::string_view> path)
  {
    if (pattern.empty())
    {
      return path.empty();
    }
    if (pattern.front() == "**")
    {
      for (std::size_t skip = 0; skip <= path.size(); ++skip)
      {
        if (match_segments(pattern.subspan(1), path.subspan(skip)))
        {
          return true;
        }
      }
      return false;
    }
    return !path.empty() && match_segment(pattern.front(), path.front())
      && match_segments(pattern.subspan(1), path.subspan(1));
  }

  /// Check whether a path matches any exclude pattern.
  ///
  /// \param path Relative path.
  /// \param excludes Glob patterns.
  /// \return True when excluded.
  bool
  is_excluded(std::string_view path, const std::vector<std::string> &excludes)
  {
    return std::ranges::any_of(
      excludes, [path](const std::string &pattern) { return Comprendo::glob_match(pattern, path); }
    );
  }

  /// Normalize a configured root to a '/'-separated prefix without "./".
  ///
  /// \param root Configured root, e.g. "." or "src/".
  /// \return Normalized relative path; empty for the whole tree.
  std::string
  normalize_root(std::string_view root)
  {
    std::string normalized = std::filesystem::path {root}.lexically_normal().generic_string();
    while (!normalized.empty() && normalized.back() == '/')
    {
      normalized.pop_back();
    }
    return normalized == "." ? std::string {} : normalized;
  }

  /// List files with git, or report that the root is not a usable work tree.
  ///
  /// \param root Project root.
  /// \param roots Normalized subdirectories.
  /// \return Paths, or an error message when git cannot list the tree.
  std::expected<std::vector<std::string>, std::string>
  list_with_git(const std::filesystem::path &root, const std::vector<std::string> &roots)
  {
    const auto inside = Comprendo::run_process({"git", "rev-parse", "--is-inside-work-tree"}, root);
    if (!inside || inside->exit_code != 0)
    {
      return std::unexpected(inside ? inside->err : inside.error());
    }

    std::vector<std::string> command
      = {"git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--"};
    for (const std::string &sub : roots)
    {
      command.push_back(sub.empty() ? "." : sub);
    }
    const auto listed = Comprendo::run_process(command, root);
    if (!listed || listed->exit_code != 0)
    {
      return std::unexpected(listed ? listed->err : listed.error());
    }

    std::vector<std::string> files;
    for (const auto entry : std::views::split(std::string_view {listed->out}, '\0'))
    {
      if (!entry.empty())
      {
        files.emplace_back(entry.begin(), entry.end());
      }
    }
    return files;
  }

  /// List files by walking the directory tree, skipping `.git` directories.
  ///
  /// \param root Project root.
  /// \param roots Normalized subdirectories.
  /// \return Paths relative to \p root.
  std::vector<std::string>
  list_with_walk(const std::filesystem::path &root, const std::vector<std::string> &roots)
  {
    std::vector<std::string> files;
    for (const std::string &sub : roots)
    {
      std::error_code error;
      std::filesystem::recursive_directory_iterator it {
        root / sub, std::filesystem::directory_options::skip_permission_denied, error
      };
      for (; !error && it != std::filesystem::recursive_directory_iterator {}; it.increment(error))
      {
        if (it->is_directory(error) && it->path().filename() == ".git")
        {
          it.disable_recursion_pending();
          continue;
        }
        if (it->is_regular_file(error))
        {
          files.push_back(it->path().lexically_relative(root).generic_string());
        }
      }
      if (error)
      {
        Comprendo::Logger::warn("walking {} stopped: {}", (root / sub).string(), error.message());
      }
    }
    return files;
  }
} // namespace

namespace Comprendo
{
  bool
  glob_match(std::string_view pattern, std::string_view path)
  {
    const std::vector<std::string_view> path_segments = split_segments(path);
    if (pattern.find('/') == std::string_view::npos)
    {
      return !path_segments.empty() && match_segment(pattern, path_segments.back());
    }
    const std::vector<std::string_view> pattern_segments = split_segments(pattern);
    return match_segments(pattern_segments, path_segments);
  }

  std::expected<FileListing, std::string>
  enumerate_files(
    const std::filesystem::path &root,
    const std::vector<std::string> &roots,
    const std::vector<std::string> &excludes
  )
  {
    const ProfileScope profile {"enumerate_files"};

    if (!std::filesystem::is_directory(root))
    {
      return std::unexpected(fmt::format("source root {} is not a directory", root.string()));
    }
    std::vector<std::string> subdirs;
    subdirs.reserve(roots.size());
    for (const std::string &sub : roots)
    {
      subdirs.push_back(normalize_root(sub));
    }
    if (subdirs.empty())
    {
      subdirs.emplace_back();
    }

    FileListing listing;
    if (auto from_git = list_with_git(root, subdirs))
    {
      listing.source = FileSource::Git;
      listing.files = std::move(*from_git);
    }
    else
    {
      Logger::debug("git listing unavailable for {}, walking instead", root.string());
      listing.source = FileSource::Walk;
      listing.files = list_with_walk(root, subdirs);
    }

    std::erase_if(
      listing.files,
      [&](const std::string &file)
      { return is_excluded(file, excludes) || !std::filesystem::is_regular_file(root / file); }
    );
    std::ranges::sort(listing.files);
    const auto duplicates = std::ranges::unique(listing.files);
    listing.files.erase(duplicates.begin(), duplicates.end());
    return listing;
  }
} // namespace Comprendo
