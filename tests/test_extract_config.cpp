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
#include "TempDir.hpp"

using Catch::Matchers::ContainsSubstring;
using Comprendo::ExtractConfig;

/// Every recognized key replaces its default.
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
)",
    "config.toml"
  );
  REQUIRE(applied.has_value());
  CHECK(config.roots == std::vector<std::string> {"src", "include"});
  CHECK(config.excludes == std::vector<std::string> {"third_party/**"});
  CHECK(config.languages == std::vector<std::string> {"C++"});
  CHECK(config.include_paths == std::vector<std::string> {"include"});
  CHECK(config.ctags == "/opt/ctags/bin/ctags");
  CHECK(config.ctags_batch == 16);
}

/// An empty document keeps the defaults.
TEST_CASE("empty config keeps defaults", "[config]")
{
  ExtractConfig config;
  REQUIRE(Comprendo::apply_config_toml(config, "", "config.toml").has_value());
  CHECK(config.roots == std::vector<std::string> {"."});
  CHECK(config.ctags == "ctags");
  CHECK(config.languages == std::vector<std::string> {"C++"});
}

/// Mistakes are reported with the source name and the offending key.
TEST_CASE("config toml rejects mistakes", "[config]")
{
  const auto error_for = [](const char *text)
  {
    ExtractConfig config;
    const auto applied = Comprendo::apply_config_toml(config, text, "my.toml");
    REQUIRE_FALSE(applied.has_value());
    return applied.error();
  };
  CHECK_THAT(error_for("[extract]\nrots = []"), ContainsSubstring("unknown key extract.rots"));
  CHECK_THAT(
    error_for("[extract]\nroots = \"src\""), ContainsSubstring("extract.roots must be an array")
  );
  CHECK_THAT(error_for("[extract]\nexcludes = [1]"), ContainsSubstring("only strings"));
  CHECK_THAT(error_for("[extract]\nctags = 3"), ContainsSubstring("extract.ctags must be"));
  CHECK_THAT(error_for("[extract]\nctags_batch = 0"), ContainsSubstring("positive integer"));
  CHECK_THAT(error_for("[extrct]\nroots = []"), ContainsSubstring("unknown section [extrct]"));
  CHECK_THAT(error_for("extract = 1"), ContainsSubstring("extract must be a table"));
  CHECK_THAT(error_for("[extract\nroots = []"), ContainsSubstring("my.toml"));
}

/// COMPRENDO_CTAGS replaces the executable; an absent variable changes nothing.
TEST_CASE("environment overrides the ctags executable", "[config]")
{
  ExtractConfig config;
  REQUIRE(
    Comprendo::apply_environment(config, std::unordered_map<std::string, std::string> {})
      .has_value()
  );
  CHECK(config.ctags == "ctags");

  REQUIRE(
    Comprendo::apply_environment(
      config, std::unordered_map<std::string, std::string> {{"COMPRENDO_CTAGS", "/x/ctags"}}
    )
      .has_value()
  );
  CHECK(config.ctags == "/x/ctags");
}

/// The project's `.comprendo/config.toml` is picked up from the root.
TEST_CASE("config is loaded from the project root", "[config]")
{
  const Comprendo::Testing::TempDir dir {"config-load"};
  std::filesystem::create_directories(dir / ".comprendo");
  std::ofstream {dir / ".comprendo/config.toml"} << "[extract]\nroots = [\"lib\"]\n";

  const auto config = Comprendo::load_extract_config(dir.path());
  REQUIRE(config.has_value());
  CHECK(config->root == dir.path());
  CHECK(config->roots == std::vector<std::string> {"lib"});

  std::ofstream {dir / ".comprendo/config.toml"} << "[extract]\nbogus = 1\n";
  const auto broken = Comprendo::load_extract_config(dir.path());
  REQUIRE_FALSE(broken.has_value());
  CHECK_THAT(broken.error(), ContainsSubstring("config.toml"));
}
