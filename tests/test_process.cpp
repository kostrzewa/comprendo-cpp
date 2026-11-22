/// \file
/// \brief Tests for running child processes.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>

#include "Comprendo/Process.hpp"
#include "TempDir.hpp"

using Catch::Matchers::ContainsSubstring;
using Comprendo::run_process;

/// Standard output, standard error, and the exit code are captured separately.
TEST_CASE("process captures output and exit code", "[process]")
{
  const auto result = run_process({"sh", "-c", "printf out; printf err >&2; exit 3"});
  REQUIRE(result.has_value());
  CHECK(result->out == "out");
  CHECK(result->err == "err");
  CHECK(result->exit_code == 3);
}

/// The child runs in the requested working directory.
TEST_CASE("process honours the working directory", "[process]")
{
  const Comprendo::Testing::TempDir dir {"process-cwd"};
  const auto result = run_process({"pwd", "-P"}, dir.path());
  REQUIRE(result.has_value());
  CHECK(result->out == std::filesystem::canonical(dir.path()).string() + "\n");
}

/// Output larger than a pipe buffer on both streams does not deadlock.
TEST_CASE("process drains large output", "[process]")
{
  const auto result
    = run_process({"sh", "-c", "head -c 300000 /dev/zero; head -c 300000 /dev/zero >&2"});
  REQUIRE(result.has_value());
  CHECK(result->out.size() == 300000);
  CHECK(result->err.size() == 300000);
}

/// A child killed by a signal reports 128 plus the signal number.
TEST_CASE("process reports termination by signal", "[process]")
{
  const auto result = run_process({"sh", "-c", "kill -9 $$"});
  REQUIRE(result.has_value());
  CHECK(result->exit_code == 128 + 9);
}

/// A missing program is an error rather than a result.
TEST_CASE("process reports a missing program", "[process]")
{
  const auto result = run_process({"comprendo-no-such-program-xyz"});
  REQUIRE_FALSE(result.has_value());
  CHECK_THAT(result.error(), ContainsSubstring("cannot run 'comprendo-no-such-program-xyz'"));
  CHECK_FALSE(run_process({}).has_value());
}
