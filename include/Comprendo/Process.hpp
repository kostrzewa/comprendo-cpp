#pragma once

/// \file
/// \brief Running external tools (git, ctags) and capturing their output.

#include <chrono>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
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

  /// A running child process whose standard input and output are connected to
  /// this process through pipes; its standard error is discarded.
  ///
  /// The first spawn sets `SIGPIPE` to ignored for the whole process, so writing
  /// to a child that has exited reports an error instead of terminating.
  class Subprocess
  {
  public:
    /// Start a program.
    ///
    /// \param argv Program followed by its arguments; must not be empty.
    /// \param cwd Working directory for the child, or empty to inherit.
    /// \return The running process, or an error message.
    [[nodiscard]]
    static std::expected<Subprocess, std::string>
    spawn(const std::vector<std::string> &argv, const std::filesystem::path &cwd = {});

    /// Close the pipes and wait for the child, killing it when still running.
    ~Subprocess();

    /// Take over a running process.
    ///
    /// \param other Process to move from; left without a child.
    Subprocess(Subprocess &&other) noexcept;

    /// Take over a running process, finishing the current one first.
    ///
    /// \param other Process to move from; left without a child.
    /// \return This process.
    Subprocess &
    operator=(Subprocess &&other) noexcept;

    Subprocess(const Subprocess &) = delete;
    Subprocess &
    operator=(const Subprocess &) = delete;

    /// Write all bytes to the child's standard input.
    ///
    /// \param data Bytes to write.
    /// \return Nothing, or an error message (for example when the child exited).
    [[nodiscard]]
    std::expected<void, std::string>
    write(std::string_view data);

    /// Read whatever the child has written to standard output, waiting up to a timeout.
    ///
    /// \param timeout Longest time to wait for data.
    /// \return At least one byte, or an error message on timeout or end of output.
    [[nodiscard]]
    std::expected<std::string, std::string>
    read_some(std::chrono::milliseconds timeout);

    /// Close standard input and wait for the child to exit.
    ///
    /// \param timeout Longest time to wait before killing the child.
    /// \return Exit status, or 128 plus the signal number.
    int
    finish(std::chrono::milliseconds timeout);

  private:
    /// Wrap a started child.
    ///
    /// \param pid Child process id.
    /// \param input Write end of the child's standard input.
    /// \param output Read end of the child's standard output.
    Subprocess(int pid, int input, int output) : m_pid(pid), m_input(input), m_output(output) { }

    /// Child process id, or -1 when there is none.
    int m_pid = -1;
    /// Write end of the child's standard input, or -1.
    int m_input = -1;
    /// Read end of the child's standard output, or -1.
    int m_output = -1;
  };
} // namespace Comprendo
