/// \file
/// \brief Tests for extractor configuration from TOML and the environment.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Comprendo/ExtractConfig.hpp"
#include "Checks.hpp"
#include "TempDir.hpp"

using Catch::Matchers::ContainsSubstring;
using Comprendo::ExtractConfig;

/// Users tune extraction through `.comprendo/config.toml`, so every documented key
/// must take effect. Applies a document setting every key in `[extract]` and
/// checks each field of ExtractConfig holds the configured value instead of its
/// default.
TEST_CASE("config toml overrides defaults", "[config]")
{
  ExtractConfig config;
  const auto applied = Comprendo::apply_config_toml(
    config,
    R"([extract]
roots = ["src", "include"]
excludes = ["third_party/**"]
languages = ["C++"]
include_paths = ["include"]
ctags = "/opt/ctags/bin/ctags"
ctags_batch = 16
backend = "clangd"
clangd = "/opt/llvm/bin/clangd"
compile_commands_dir = "out/debug"
)",
    "config.toml"
  );
  CHECK(applied.has_value());
  CHECK(config.roots == std::vector<std::string> {"src", "include"});
  CHECK(config.excludes == std::vector<std::string> {"third_party/**"});
  CHECK(config.languages == std::vector<std::string> {"C++"});
  CHECK(config.include_paths == std::vector<std::string> {"include"});
  CHECK(config.ctags == "/opt/ctags/bin/ctags");
  CHECK(config.ctags_batch == 16);
  CHECK(config.backend == Comprendo::ExtractBackend::Clangd);
  CHECK(config.clangd == "/opt/llvm/bin/clangd");
  CHECK(config.compile_commands_dir == "out/debug");
}

/// An empty or absent config file is the common case and must leave the defaults
/// intact, including the C++-only language list. Applies an empty document and
/// checks the roots, ctags executable, and languages keep their default values.
TEST_CASE("empty config keeps defaults", "[config]")
{
  ExtractConfig config;
  CHECK(Comprendo::apply_config_toml(config, "", "config.toml").has_value());
  CHECK(config.roots == std::vector<std::string> {"."});
  CHECK(config.ctags == "ctags");
  CHECK(config.languages == std::vector<std::string> {"C++"});
}

/// Silently ignoring a typo would extract the wrong files without any hint, so
/// mistakes must be errors naming the key and file. Applies documents with a
/// misspelled key, wrong value types, a non-positive batch size, an unknown
/// section, a non-table section, and a syntax error, and checks each message.
TEST_CASE("config toml rejects mistakes", "[config]")
{
  const auto error_for = [](const char *text)
  {
    ExtractConfig config;
    const auto applied = Comprendo::apply_config_toml(config, text, "my.toml");
    CHECK_FALSE(applied.has_value());
    return Comprendo::Testing::error_of(applied);
  };
  CHECK_THAT(error_for("[extract]\nrots = []"), ContainsSubstring("unknown key extract.rots"));
  CHECK_THAT(
    error_for("[extract]\nroots = \"src\""), ContainsSubstring("extract.roots must be an array")
  );
  CHECK_THAT(error_for("[extract]\nexcludes = [1]"), ContainsSubstring("only strings"));
  CHECK_THAT(error_for("[extract]\nctags = 3"), ContainsSubstring("extract.ctags must be"));
  CHECK_THAT(error_for("[extract]\nctags_batch = 0"), ContainsSubstring("positive integer"));
  CHECK_THAT(error_for("[extract]\nbackend = \"lsp\""), ContainsSubstring("extract.backend"));
  CHECK_THAT(error_for("[extract]\nclangd = \"\""), ContainsSubstring("non-empty string"));
  CHECK_THAT(error_for("[extract]\ncompile_commands_dir = 1"), ContainsSubstring("a string"));
  CHECK_THAT(error_for("[extrct]\nroots = []"), ContainsSubstring("unknown section [extrct]"));
  CHECK_THAT(error_for("extract = 1"), ContainsSubstring("extract must be a table"));
  CHECK_THAT(error_for("[extract\nroots = []"), ContainsSubstring("my.toml"));
}

/// COMPRENDO_CTAGS lets users point at a specific ctags without editing config
/// files; it must override the config and do nothing when unset. Injects a custom
/// environment map (so the test does not depend on the real environment), first
/// empty, then with the variable set, and checks the configured executable.
TEST_CASE("environment overrides the ctags executable", "[config]")
{
  ExtractConfig config;
  CHECK(
    Comprendo::apply_environment(config, std::unordered_map<std::string, std::string> {})
      .has_value()
  );
  CHECK(config.ctags == "ctags");

  CHECK(
    Comprendo::apply_environment(
      config, std::unordered_map<std::string, std::string> {{"COMPRENDO_CTAGS", "/x/ctags"}}
    )
      .has_value()
  );
  CHECK(config.ctags == "/x/ctags");
}

/// COMPRENDO_CLANGD selects the clangd binary the same way COMPRENDO_CTAGS selects
/// ctags, e.g. to use an unversioned or self-built clangd. Injects an environment
/// map with the variable set and checks the clangd executable changes while the
/// ctags executable keeps its default.
TEST_CASE("environment overrides the clangd executable", "[config]")
{
  ExtractConfig config;
  CHECK(config.clangd == "clangd-22");
  CHECK(
    Comprendo::apply_environment(
      config, std::unordered_map<std::string, std::string> {{"COMPRENDO_CLANGD", "/x/clangd"}}
    )
      .has_value()
  );
  CHECK(config.clangd == "/x/clangd");
  CHECK(config.ctags == "ctags");
}

/// Backend names come from both the TOML file and `extract --backend`, so the
/// parser shared by both must accept exactly the documented names. Parses each
/// valid name and two invalid ones and checks the result.
TEST_CASE("backend names parse", "[config]")
{
  CHECK(Comprendo::parse_extract_backend("auto") == Comprendo::ExtractBackend::Auto);
  CHECK(Comprendo::parse_extract_backend("ctags") == Comprendo::ExtractBackend::Ctags);
  CHECK(Comprendo::parse_extract_backend("clangd") == Comprendo::ExtractBackend::Clangd);
  CHECK_FALSE(Comprendo::parse_extract_backend("Clangd").has_value());
  CHECK_FALSE(Comprendo::parse_extract_backend("").has_value());
}

/// load_extract_config() must find `.comprendo/config.toml` under the given root
/// and surface its errors. Writes a config into a scratch project, loads it and
/// checks the root and roots, then rewrites it with an unknown key and checks
/// loading fails with the file named in the error.
TEST_CASE("config is loaded from the project root", "[config]")
{
  const Comprendo::Testing::TempDir dir {"config-load"};
  std::filesystem::create_directories(dir / ".comprendo");
  std::ofstream {dir / ".comprendo/config.toml"} << "[extract]\nroots = [\"lib\"]\n";

  const auto config = Comprendo::load_extract_config(dir.path());
  CHECK(config.has_value());
  if (!config)
  {
    return;
  }
  CHECK(config->root == dir.path());
  CHECK(config->roots == std::vector<std::string> {"lib"});

  std::ofstream {dir / ".comprendo/config.toml"} << "[extract]\nbogus = 1\n";
  const auto broken = Comprendo::load_extract_config(dir.path());
  CHECK_FALSE(broken.has_value());
  CHECK_THAT(Comprendo::Testing::error_of(broken), ContainsSubstring("config.toml"));
}
