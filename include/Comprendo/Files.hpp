#pragma once

/// \file
/// \brief Source-file enumeration with `git ls-files` and a directory-walk fallback.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Comprendo
{
  /// How a file listing was obtained.
  enum class FileSource : std::uint8_t
  {
    /// Tracked and untracked-but-not-ignored files from `git ls-files`.
    Git,
    /// Recursive directory walk; used when the root is not inside a git work tree.
    Walk,
  };

  /// A sorted list of repository-relative file paths.
  struct FileListing
  {
    /// Where the paths came from.
    FileSource source = FileSource::Walk;
    /// Paths relative to the root, '/'-separated, sorted, unique.
    std::vector<std::string> files;
  };

  /// Match a '/'-separated relative path against a glob pattern.
  ///
  /// `*` matches any run of characters within one path segment, `?` matches one
  /// character other than '/', and a `**` segment matches zero or more whole
  /// segments. A pattern without '/' is matched against the file name only, so
  /// `*.md` excludes Markdown files at any depth.
  ///
  /// \param pattern Glob pattern.
  /// \param path Relative path to test.
  /// \return True when \p path matches \p pattern.
  [[nodiscard]]
  bool
  glob_match(std::string_view pattern, std::string_view path);

  /// List the files below \p root.
  ///
  /// Uses `git ls-files --cached --others --exclude-standard` when \p root is in a
  /// git work tree, otherwise walks the directories (skipping `.git`). Each entry
  /// of \p roots restricts the listing to that subdirectory; paths matching any of
  /// \p excludes are dropped, and so are listed paths that no longer exist.
  ///
  /// \param root Project root; returned paths are relative to it.
  /// \param roots Subdirectories to include, relative to \p root ("." for all).
  /// \param excludes Glob patterns (see glob_match()) of paths to drop.
  /// \return The listing, or an error message.
  [[nodiscard]]
  std::expected<FileListing, std::string>
  enumerate_files(
    const std::filesystem::path &root,
    const std::vector<std::string> &roots,
    const std::vector<std::string> &excludes
  );
} // namespace Comprendo
