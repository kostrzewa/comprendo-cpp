/// \file
/// \brief Tests for the `comprendo-cli` command dispatcher.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Comprendo/Cli.hpp"
#include "Comprendo/Process.hpp"
#include "TempDir.hpp"
#include "Version.hpp"

namespace
{
  using Catch::Matchers::ContainsSubstring;

  /// Captured result of one CLI invocation.
  struct CliRun
  {
    /// Exit code.
    int code = 0;
    /// Standard output.
    std::string out;
    /// Standard error.
    std::string err;
  };

  /// Run the CLI in-process with captured streams.
  ///
  /// \param args Arguments after the program name.
  /// \return Exit code and captured output.
  CliRun
  run(std::initializer_list<std::string_view> args)
  {
    const std::vector<std::string_view> arguments(args);
    std::ostringstream out;
    std::ostringstream err;
    const int code = Comprendo::run_cli(arguments, out, err);
    return {.code = code, .out = out.str(), .err = err.str()};
  }
} // namespace

/// Scripts and users rely on `--help` and `--version` succeeding and printing to
/// standard output, and the version must identify the build. Runs top-level help,
/// `extract --help`, and `--version` through run_cli() with captured streams, and
/// checks the exit code, that CLI11's generated help lists the subcommands (and
/// extract's options) without touching stderr, and that the version line contains
/// the project version and git commit.
TEST_CASE("cli prints help and version", "[cli]")
{
  const CliRun help = run({"--help"});
  CHECK(help.code == Comprendo::kExitOk);
  CHECK_THAT(help.out, ContainsSubstring("comprendo-cli [OPTIONS] SUBCOMMAND"));
  CHECK_THAT(help.out, ContainsSubstring("extract"));
  CHECK_THAT(help.out, ContainsSubstring("validate"));
  CHECK_THAT(help.out, ContainsSubstring("info"));
  CHECK(help.err.empty());

  const CliRun extract_help = run({"extract", "--help"});
  CHECK(extract_help.code == Comprendo::kExitOk);
  CHECK_THAT(extract_help.out, ContainsSubstring("--root"));
  CHECK_THAT(extract_help.out, ContainsSubstring("--output"));

  const CliRun version = run({"--version"});
  CHECK(version.code == Comprendo::kExitOk);
  CHECK_THAT(version.out, ContainsSubstring(COMPRENDO_VERSION));
  CHECK_THAT(version.out, ContainsSubstring(COMPRENDO_GIT_COMMIT));
}

/// Malformed invocations must be distinguishable from failed commands (exit code
/// 2 versus 1) and must explain what was wrong. Runs a table of bad command lines
/// (no command, unknown command, missing or unknown extract options, extra or
/// option-like positional arguments) and checks each exits with the usage code,
/// prints CLI11's message plus the `--help` hint to stderr, and nothing to stdout.
TEST_CASE("cli rejects malformed command lines", "[cli]")
{
  const auto check_usage = [](std::initializer_list<std::string_view> args, const char *message)
  {
    const CliRun result = run(args);
    CHECK(result.code == Comprendo::kExitUsage);
    CHECK_THAT(result.err, ContainsSubstring(message));
    CHECK_THAT(result.err, ContainsSubstring("Run with --help for more information."));
    CHECK(result.out.empty());
  };
  check_usage({}, "A subcommand is required");
  check_usage({"frobnicate"}, "Unknown command 'frobnicate'");
  check_usage({"extract", "--root"}, "--root: 1 required TEXT missing");
  check_usage({"extract", "--bogus", "x"}, "not expected: --bogus x");
  check_usage({"validate", "a", "b"}, "not expected: b");
  check_usage({"info", "--json"}, "not expected: --json");
}

/// Commands that run but fail must exit with code 1 and name the problem so users
/// can act on it. Runs validate and info on a missing file and validate on a text
/// file, and checks the failure exit code and the messages on stderr.
TEST_CASE("cli reports failures", "[cli]")
{
  const Comprendo::Testing::TempDir dir {"cli-failures"};
  const std::string missing = (dir / "absent.sqlite").string();
  const CliRun validate = run({"validate", missing});
  CHECK(validate.code == Comprendo::kExitFailure);
  CHECK_THAT(validate.err, ContainsSubstring("invalid chart"));
  CHECK_THAT(validate.err, ContainsSubstring("does not exist"));

  const CliRun info = run({"info", missing});
  CHECK(info.code == Comprendo::kExitFailure);
  CHECK_THAT(info.err, ContainsSubstring("does not exist"));

  std::ofstream {dir / "bad.sqlite"} << "not a chart at all, just text long enough";
  const CliRun garbage = run({"validate", (dir / "bad.sqlite").string()});
  CHECK(garbage.code == Comprendo::kExitFailure);
  CHECK_THAT(garbage.err, ContainsSubstring("invalid chart"));
}

/// This is the M5 acceptance path: extract a tree, validate the chart, summarize
/// it, all headless. Copies the fixture to a scratch directory outside git, runs
/// extract with the default output, then validate and info on the result, checking
/// exit codes and key summary lines; finally extracts again with --output to a new
/// directory. Skipped when Universal Ctags is not installed.
TEST_CASE("cli extract validate info pipeline", "[cli][fixture]")
{
  const auto ctags = Comprendo::run_process({"ctags", "--version"});
  if (!ctags || ctags->exit_code != 0 || !ctags->out.contains("Universal Ctags"))
  {
    SKIP("Universal Ctags is not installed");
  }
  const Comprendo::Testing::TempDir dir {"cli-pipeline"};
  const std::filesystem::path root = dir / "project";
  std::filesystem::copy(
    COMPRENDO_FIXTURE_DIR "/extract_basic", root, std::filesystem::copy_options::recursive
  );

  const CliRun extract = run({"extract", "--root", root.string()});
  INFO(extract.err);
  CHECK(extract.code == Comprendo::kExitOk);
  const std::filesystem::path chart = root / Comprendo::kDefaultChartPath;
  CHECK(std::filesystem::exists(chart));
  CHECK_THAT(extract.out, ContainsSubstring("wrote " + chart.string()));

  const CliRun validate = run({"validate", chart.string()});
  CHECK(validate.code == Comprendo::kExitOk);
  CHECK(validate.out == chart.string() + ": valid\n");

  const CliRun info = run({"info", chart.string()});
  CHECK(info.code == Comprendo::kExitOk);
  CHECK_THAT(info.out, ContainsSubstring("project:      project"));
  CHECK_THAT(info.out, ContainsSubstring("  File         5\n"));
  CHECK_THAT(info.out, ContainsSubstring("  Includes     3\n"));
  CHECK_THAT(info.out, ContainsSubstring("layout:       none"));

  const std::filesystem::path custom = dir / "custom/out.sqlite";
  const CliRun custom_extract
    = run({"extract", "--root", root.string(), "--output", custom.string()});
  CHECK(custom_extract.code == Comprendo::kExitOk);
  CHECK(std::filesystem::exists(custom));
}
