#pragma once

/// \file
/// \brief The `comprendo-cli` command dispatcher: `extract`, `validate`, `info`.

#include <ostream>
#include <span>
#include <string_view>

namespace Comprendo
{
  /// Process exit codes of `comprendo-cli`.
  enum ExitCode : int
  {
    /// The command succeeded.
    kExitOk = 0,
    /// The command ran and failed: invalid chart, extraction or I/O error.
    kExitFailure = 1,
    /// The command line was malformed.
    kExitUsage = 2,
  };

  /// Default chart location relative to the project root.
  inline constexpr std::string_view kDefaultChartPath = ".comprendo/chart.sqlite";

  /// Run `comprendo-cli` with the given arguments.
  ///
  /// Results go to \p out and diagnostics to \p err; logging goes through
  /// Comprendo::Logger, which the caller initializes.
  ///
  /// \param args Arguments after the program name.
  /// \param out Stream for command results (summaries, help, version).
  /// \param err Stream for usage errors and failure details.
  /// \return One of the ExitCode values.
  [[nodiscard]]
  int
  run_cli(std::span<const std::string_view> args, std::ostream &out, std::ostream &err);
} // namespace Comprendo
