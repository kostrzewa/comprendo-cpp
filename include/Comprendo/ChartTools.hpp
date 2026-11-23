#pragma once

/// \file
/// \brief The chart tools an assistant uses during `/chart`: look up, annotate, and extend a chart.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Comprendo/Json.hpp"

namespace Comprendo
{
  /// One tool as offered to the model.
  struct ToolInfo
  {
    /// Tool name, e.g. `chart_overview`.
    std::string name;
    /// What the tool does, shown to the model.
    std::string description;
    /// JSON Schema of the argument object.
    Json input_schema;
  };

  /// The chart tools of one project.
  ///
  /// Every call opens `ROOT/.comprendo/chart.sqlite` (creating it when absent),
  /// runs in one immediate transaction, and closes the chart again, so the
  /// extractor may replace the file between calls and the viewer sees each
  /// call's changes at once. Arguments are checked by the tools themselves:
  /// mistakes come back as error text the model can act on, and nothing is
  /// written by a failed call.
  class ChartTools
  {
  public:
    /// Create the tools for a project.
    ///
    /// \param root Project directory; relative file arguments are relative to it.
    explicit ChartTools(const std::filesystem::path &root);

    /// List the tools.
    ///
    /// \return Tools in a stable order, overview first.
    [[nodiscard]]
    const std::vector<ToolInfo> &
    tools() const
    {
      return m_tools;
    }

    /// Run a tool.
    ///
    /// \param name Tool name.
    /// \param args Argument object from the model; null counts as empty.
    /// \return The tool's output, or an error message for the model.
    [[nodiscard]]
    std::expected<std::string, std::string>
    call(std::string_view name, const Json &args) const;

    /// Chart file the tools work on.
    ///
    /// \return `ROOT/.comprendo/chart.sqlite`.
    [[nodiscard]]
    const std::filesystem::path &
    chart_path() const
    {
      return m_chart;
    }

  private:
    /// Project directory.
    std::filesystem::path m_root;
    /// Chart file.
    std::filesystem::path m_chart;
    /// Tool descriptions.
    std::vector<ToolInfo> m_tools;
  };

  /// Compute a cluster id from its label: FNV-1a over `"cluster" 0x1F label`,
  /// with 0 (reserved for "no cluster") replaced by 1.
  ///
  /// \param label Cluster label.
  /// \return Cluster id, never 0.
  [[nodiscard]]
  std::uint64_t
  make_cluster_id(std::string_view label);

  /// Format an id the way the tools show it: `0x` and 16 hex digits.
  ///
  /// \param id Node or cluster id.
  /// \return Hex text.
  [[nodiscard]]
  std::string
  format_id(std::uint64_t id);

  /// Parse an id given by the model: hex with `0x`, or unsigned decimal.
  ///
  /// \param text Id text; surrounding whitespace is ignored.
  /// \return The id, or nothing when malformed or out of range.
  [[nodiscard]]
  std::optional<std::uint64_t>
  parse_id(std::string_view text);
} // namespace Comprendo
