#pragma once

/// \file
/// \brief Universal Ctags extractor: symbol inventory, `Contains` and `Includes` edges.

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Comprendo/Chart.hpp"
#include "Comprendo/Extractor.hpp"
#include "Comprendo/Includes.hpp"

namespace Comprendo
{
  /// One symbol reported by `ctags --output-format=json`.
  struct CtagsTag
  {
    /// Unqualified name.
    std::string name;
    /// Repository-relative file path.
    std::string path;
    /// Long kind name, e.g. "class" or "function".
    std::string kind;
    /// Enclosing scope's qualified name, empty at file level.
    std::string scope;
    /// Long kind name of the enclosing scope.
    std::string scope_kind;
    /// Parameter list and qualifiers for callables, e.g. "(int a) const".
    std::string signature;
    /// 1-based line of the definition.
    std::uint32_t line = 0;
    /// 1-based last line, or 0 when ctags does not know it.
    std::uint32_t end = 0;
    /// True for a deleted function declaration (`= delete`).
    bool deleted = false;

    /// Memberwise equality.
    bool
    operator==(const CtagsTag &) const = default;
  };

  /// A source file as seen by the ctags extractor.
  struct CtagsSourceFile
  {
    /// Repository-relative path.
    std::string path;
    /// ctags language name, e.g. "C++".
    std::string language;
    /// Number of lines.
    std::uint32_t lines = 0;
    /// Include directives (C-family files only).
    std::vector<IncludeDirective> includes;
  };

  /// Parse one line of ctags JSON output.
  ///
  /// \param line One JSON object.
  /// \return The tag, nothing for non-tag records (such as pseudo tags), or an
  ///         error message for malformed input.
  [[nodiscard]]
  std::expected<std::optional<CtagsTag>, std::string>
  parse_ctags_line(std::string_view line);

  /// Map a ctags kind to a node kind.
  ///
  /// \param language ctags language of the file.
  /// \param kind Long kind name of the tag.
  /// \param scope_kind Long kind name of the enclosing scope, empty at file level;
  ///        functions inside class-like scopes become methods.
  /// \return The node kind, or nothing for kinds that are not charted
  ///         (locals, parameters, labels, include references). Prototypes map
  ///         like functions.
  [[nodiscard]]
  std::optional<NodeKind>
  map_ctags_kind(std::string_view language, std::string_view kind, std::string_view scope_kind);

  /// Build a chart from files and tags. Pure and deterministic.
  ///
  /// Each file becomes a File node. Each charted definition becomes a node whose
  /// id is derived from its kind, qualified name, file, and signature; later
  /// duplicates of an id are dropped. A prototype is matched against the
  /// definitions with the same qualified name (exact signature, then parameter
  /// types without names and defaults, then a unique definition with the same
  /// arity and qualifiers); only unmatched, non-deleted prototypes become nodes,
  /// so pure virtual and externally defined functions are charted at their
  /// declaration. Anonymous callables (ctags `__anon*` names, i.e. lambdas) are
  /// skipped. Every symbol gets exactly one `Contains` parent: its
  /// enclosing scope when that resolves (same file preferred), otherwise its file.
  /// Resolved include directives become `Includes` edges between files.
  ///
  /// \param files Source files; tags in other files are ignored.
  /// \param tags Tags in any order.
  /// \param include_paths Include search paths (see resolve_include()).
  /// \return Chart in canonical order, header fields empty.
  [[nodiscard]]
  Chart
  build_ctags_chart(
    const std::vector<CtagsSourceFile> &files,
    const std::vector<CtagsTag> &tags,
    const std::vector<std::string> &include_paths
  );

  /// Extractor backed by the Universal Ctags executable.
  class CtagsExtractor final : public Extractor
  {
  public:
    /// Backend name.
    ///
    /// \return "ctags".
    [[nodiscard]]
    std::string_view
    name() const override
    {
      return "ctags";
    }

    /// Run ctags over the configured tree and build the chart.
    ///
    /// \param config Extraction settings.
    /// \return The chart, or an error message (for example when ctags is missing
    ///         or is not Universal Ctags).
    [[nodiscard]]
    std::expected<Chart, std::string>
    extract(const ExtractConfig &config) override;
  };
} // namespace Comprendo
