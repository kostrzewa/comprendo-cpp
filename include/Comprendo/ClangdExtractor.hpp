#pragma once

/// \file
/// \brief clangd-22 enrichment: call and inheritance edges resolved semantically.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Comprendo/Chart.hpp"
#include "Comprendo/Extractor.hpp"

namespace Comprendo
{
  /// Column of a symbol's name in its declaration line, for LSP positions.
  ///
  /// Looks for \p name as a whole identifier; for callables the occurrence
  /// followed by '(' wins (so `Circle::Circle(` picks the constructor name),
  /// otherwise the first occurrence. Operator names match at `operator`.
  ///
  /// \param line Source line.
  /// \param name Symbol name as reported by ctags.
  /// \return Byte offset of the name, or nothing when it does not occur.
  [[nodiscard]]
  std::optional<std::size_t>
  find_name_column(std::string_view line, std::string_view name);

  /// Locate the directory holding `compile_commands.json`.
  ///
  /// \param root Project root.
  /// \param configured Directory from the configuration, relative to \p root;
  ///        empty to search \p root, `root/build`, then each directory directly
  ///        below `root/build` in name order.
  /// \return The directory, or nothing when no compilation database is found.
  [[nodiscard]]
  std::optional<std::filesystem::path>
  find_compile_commands(const std::filesystem::path &root, std::string_view configured);

  /// Maps source positions reported by a language server to chart nodes.
  class NodeLocator
  {
  public:
    /// Index the nodes of a chart.
    ///
    /// \param chart Chart whose nodes are located; must outlive the locator.
    explicit NodeLocator(const Chart &chart);

    /// Find the node a position refers to.
    ///
    /// Prefers the smallest node of an accepted kind in \p file whose line range
    /// contains \p line; otherwise the only node of an accepted kind whose
    /// qualified name equals \p qualified_name.
    ///
    /// \param file Repository-relative path.
    /// \param line 1-based line.
    /// \param qualified_name Qualified name reported by the server, may be empty.
    /// \param kinds Accepted node kinds.
    /// \return Node index, or nothing.
    [[nodiscard]]
    std::optional<std::size_t>
    locate(
      std::string_view file,
      std::uint32_t line,
      std::string_view qualified_name,
      std::span<const NodeKind> kinds
    ) const;

  private:
    /// Located chart.
    const Chart *m_chart;
    /// Node indices per file.
    std::unordered_map<std::string, std::vector<std::size_t>> m_by_file;
    /// Node indices per qualified name.
    std::unordered_map<std::string, std::vector<std::size_t>> m_by_name;
  };

  /// Add `Calls` and `Inherits` edges found by clangd to a ctags chart.
  ///
  /// Starts clangd with the compilation database (no background index, so
  /// nothing is written into the project), opens every C/C++ file of the chart
  /// and waits for each to be parsed, then asks the call hierarchy for the
  /// outgoing calls of every function and method and the type hierarchy for the
  /// supertypes of every class, struct, and union. Results are mapped back to
  /// chart nodes; edges are merged per (caller, callee) with the number of call
  /// sites as weight.
  ///
  /// \param chart Chart from the ctags extractor; edges are added in place.
  /// \param root Project root the chart's paths are relative to.
  /// \param clangd clangd executable.
  /// \param compile_commands Directory holding `compile_commands.json`.
  /// \return Nothing, or an error message when clangd cannot be used.
  [[nodiscard]]
  std::expected<void, std::string>
  enrich_with_clangd(
    Chart &chart,
    const std::filesystem::path &root,
    const std::string &clangd,
    const std::filesystem::path &compile_commands
  );

  /// Extractor combining ctags (node set) with clangd (call and inheritance edges).
  class ClangdExtractor final : public Extractor
  {
  public:
    /// Create the extractor.
    ///
    /// \param required True to fail when clangd or a compilation database is
    ///        unavailable; false to fall back to the ctags chart with a warning.
    explicit ClangdExtractor(bool required) : m_required(required) { }

    /// Backend name.
    ///
    /// \return "clangd".
    [[nodiscard]]
    std::string_view
    name() const override
    {
      return "clangd";
    }

    /// Extract with ctags, then enrich with clangd.
    ///
    /// \param config Extraction settings.
    /// \return The chart, or an error message.
    [[nodiscard]]
    std::expected<Chart, std::string>
    extract(const ExtractConfig &config) override;

  private:
    /// Whether clangd problems are errors rather than warnings.
    bool m_required;
  };
} // namespace Comprendo
