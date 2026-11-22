/// \file
/// \brief Tests for running child processes.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>

#include "Comprendo/Process.hpp"
#include "Checks.hpp"
#include "TempDir.hpp"

using Catch::Matchers::ContainsSubstring;
using Comprendo::run_process;

/// The extractor inspects ctags' standard output, standard error, and exit code
/// separately, so they must not be mixed up. Runs a shell command writing to both
/// streams and exiting with 3, and checks each captured value.
TEST_CASE("process captures output and exit code", "[process]")
{
  const auto result = run_process({"sh", "-c", "printf out; printf err >&2; exit 3"});
  CHECK(result.has_value());
  if (!result)
  {
    return;
  }
  CHECK(result->out == "out");
  CHECK(result->err == "err");
  CHECK(result->exit_code == 3);
}

/// git and ctags must run in the project root so their paths are relative to it.
/// Runs `pwd -P` with a scratch directory as working directory and compares the
/// output with the directory's canonical path.
TEST_CASE("process honours the working directory", "[process]")
{
  const Comprendo::Testing::TempDir dir {"process-cwd"};
  const auto result = run_process({"pwd", "-P"}, dir.path());
  CHECK(result.has_value());
  if (!result)
  {
    return;
  }
  CHECK(result->out == std::filesystem::canonical(dir.path()).string() + "\n");
}

/// A child writing more than a pipe buffer (64 KiB) to one stream blocks if the
/// parent reads only the other; this deadlock is a classic subprocess bug. Writes
/// 300 KB to both stdout and stderr and checks everything arrives, which only
/// works because both pipes are polled concurrently.
TEST_CASE("process drains large output", "[process]")
{
  const auto result
    = run_process({"sh", "-c", "head -c 300000 /dev/zero; head -c 300000 /dev/zero >&2"});
  CHECK(result.has_value());
  if (!result)
  {
    return;
  }
  CHECK(result->out.size() == 300000);
  CHECK(result->err.size() == 300000);
}

/// A crashed tool must be reported as a failure, not as success with empty output.
/// Runs a shell that kills itself with SIGKILL and checks the exit code follows
/// the shell convention of 128 plus the signal number.
TEST_CASE("process reports termination by signal", "[process]")
{
  const auto result = run_process({"sh", "-c", "kill -9 $$"});
  CHECK(result.has_value());
  if (!result)
  {
    return;
  }
  CHECK(result->exit_code == 128 + 9);
}

/// Starting a program that does not exist is a configuration error that must be
/// reported clearly (for example a wrong `ctags` path), as must an empty command
/// line. Runs a nonexistent program and an empty argument list and checks both
/// are errors, the first naming the program.
TEST_CASE("process reports a missing program", "[process]")
{
  const auto result = run_process({"comprendo-no-such-program-xyz"});
  CHECK_FALSE(result.has_value());
  CHECK_THAT(
    Comprendo::Testing::error_of(result),
    ContainsSubstring("cannot run 'comprendo-no-such-program-xyz'")
  );
  CHECK_FALSE(run_process({}).has_value());
}
