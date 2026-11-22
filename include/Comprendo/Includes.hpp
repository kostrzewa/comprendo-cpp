#pragma once

/// \file
/// \brief Finding `#include` directives in C-family sources and resolving them to files.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Comprendo
{
  /// One `#include` directive.
  struct IncludeDirective
  {
    /// 1-based line of the directive.
    std::uint32_t line = 0;
    /// Text between the delimiters, e.g. `Comprendo/Chart.hpp`.
    std::string target;
    /// True for `<...>`, false for `"..."`.
    bool angled = false;

    /// Memberwise equality.
    bool
    operator==(const IncludeDirective &) const = default;
  };

  /// Scan source text for `#include` directives.
  ///
  /// Directives inside block comments are ignored; whitespace around `#` and
  /// `include` is allowed. Computed includes (`#include MACRO`) are skipped.
  ///
  /// \param source File contents.
  /// \return Directives in source order.
  [[nodiscard]]
  std::vector<IncludeDirective>
  scan_includes(std::string_view source);

  /// Resolve an include directive to a file of the project.
  ///
  /// Quoted includes are first looked up next to the including file; both forms
  /// then try each include path in order. Only files in \p known_files match.
  ///
  /// \param includer Repository-relative path of the including file.
  /// \param directive Directive to resolve.
  /// \param known_files Repository-relative paths of all project files.
  /// \param include_paths Directories to search, relative to the root ("." for the root).
  /// \return Repository-relative path of the included file, or nothing.
  [[nodiscard]]
  std::optional<std::string>
  resolve_include(
    std::string_view includer,
    const IncludeDirective &directive,
    const std::unordered_set<std::string> &known_files,
    const std::vector<std::string> &include_paths
  );
} // namespace Comprendo
