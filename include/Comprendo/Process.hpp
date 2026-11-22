#pragma once

/// \file
/// \brief Running external tools (git, ctags) and capturing their output.

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace Comprendo
{
  /// Outcome of a finished child process.
  struct ProcessResult
  {
    /// Exit status, or 128 plus the signal number when killed by a signal.
    int exit_code = 0;
    /// Everything the child wrote to standard output.
    std::string out;
    /// Everything the child wrote to standard error.
    std::string err;
  };

  /// Run a program to completion, capturing standard output and error.
  ///
  /// The program is looked up on `PATH` when it contains no slash. Standard input
  /// is connected to `/dev/null`.
  ///
  /// \param argv Program followed by its arguments; must not be empty.
  /// \param cwd Working directory for the child, or empty to inherit.
  /// \return The result (including non-zero exit codes), or an error message when
  ///         the program could not be started.
  [[nodiscard]]
  std::expected<ProcessResult, std::string>
  run_process(const std::vector<std::string> &argv, const std::filesystem::path &cwd = {});
} // namespace Comprendo
