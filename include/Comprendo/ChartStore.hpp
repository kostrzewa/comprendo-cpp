#pragma once

/// \file
/// \brief Reading and writing charts as SQLite databases (see plan/chart-schema.md).

#include <expected>
#include <filesystem>
#include <string>

#include "Comprendo/Chart.hpp"

namespace Comprendo
{
  /// Write a chart to a SQLite database, replacing any existing file.
  ///
  /// The chart is validated first and refused when invalid. Rows are written to a
  /// temporary file in one transaction that is renamed over \p path on success, so
  /// a failed write leaves an existing chart untouched.
  ///
  /// \param path Destination database file; missing parent directories are created.
  /// \param chart Chart to write.
  /// \return Nothing, or an error message describing why the chart was not written.
  [[nodiscard]]
  std::expected<void, std::string>
  write_chart(const std::filesystem::path &path, const Chart &chart);

  /// Read a chart from a SQLite database opened read-only.
  ///
  /// The file's `application_id` must identify a Comprendo chart and its
  /// `user_version` must be supported. The result is in canonical order (see
  /// canonicalize()). The layout is returned only when every node has a position.
  ///
  /// \param path Database file to read.
  /// \return The chart, or an error message.
  [[nodiscard]]
  std::expected<Chart, std::string>
  read_chart(const std::filesystem::path &path);
} // namespace Comprendo
