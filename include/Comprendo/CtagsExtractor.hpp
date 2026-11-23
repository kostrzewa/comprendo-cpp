#pragma once

/// \file
/// \brief Universal Ctags extractor: symbol inventory, `Contains` and `Includes` edges.

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
    /// Base classes as written (ctags `inherits`), without access specifiers
    /// and template arguments, e.g. {"Shape", "geo::Named"}.
    std::vector<std::string> bases;

    /// Memberwise equality.
    ///
    /// \return True when all members are equal.
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

  /// Where a charted definition was also declared (a matched prototype).
  struct DeclarationSite
  {
    /// Id of the definition's node.
    std::uint64_t node = 0;
    /// Repository-relative file of the declaration.
    std::string path;
    /// 1-based line of the declaration.
    std::uint32_t line = 0;

    /// Memberwise equality.
    ///
    /// \return True when all members are equal.
    bool
    operator==(const DeclarationSite &) const = default;
  };

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
  /// Resolved include directives become `Includes` edges between files. Base
  /// classes (ctags `inherits`) become `Inherits` edges from the derived class
  /// to the only class or struct the base name names, looked up in the derived
  /// class's enclosing scopes from the innermost outwards; unknown bases (for
  /// example from the standard library) and ambiguous names are skipped.
  ///
  /// \param files Source files; tags in other files are ignored.
  /// \param tags Tags in any order.
  /// \param include_paths Include search paths (see resolve_include()).
  /// \param declarations When not null, receives the location of every matched
  ///        prototype, so documentation on a header declaration can be found
  ///        for a definition in a source file.
  /// \return Chart in canonical order, header fields empty.
  [[nodiscard]]
  Chart
  build_ctags_chart(
    const std::vector<CtagsSourceFile> &files,
    const std::vector<CtagsTag> &tags,
    const std::vector<std::string> &include_paths,
    std::vector<DeclarationSite> *declarations = nullptr
  );

  /// Fill empty node summaries and details from Doxygen comments.
  ///
  /// A node takes the comment at its own line (see doc_comment_at()), else the
  /// comment at one of its declaration sites; a file node takes its `\file`
  /// comment (see file_doc_comment()). Nodes that already have a summary are
  /// left alone. Provenance stays `Extractor`, which marks the text as coming
  /// from the code rather than from the model.
  ///
  /// \param chart Chart to annotate in place.
  /// \param declarations Declaration sites from build_ctags_chart().
  /// \param lines Source lines per repository-relative path; files missing from
  ///        the map are skipped.
  /// \return Number of nodes that received a summary.
  std::size_t
  apply_doc_comments(
    Chart &chart,
    const std::vector<DeclarationSite> &declarations,
    const std::unordered_map<std::string, std::vector<std::string>> &lines
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
