#pragma once

/// \file
/// \brief The `comprendo-cli` command dispatcher: `extract`, `validate`, `info`.

#include <ostream>
#include <span>
#include <string_view>

namespace Comprendo
{
  /// Exit code: the command succeeded.
  inline constexpr int kExitOk = 0;

  /// Exit code: the command ran and failed (invalid chart, extraction or I/O error).
  inline constexpr int kExitFailure = 1;

  /// Exit code: the command line was malformed.
  inline constexpr int kExitUsage = 2;

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
  /// \return kExitOk, kExitFailure, or kExitUsage.
  [[nodiscard]]
  int
  run_cli(std::span<const std::string_view> args, std::ostream &out, std::ostream &err);
} // namespace Comprendo
