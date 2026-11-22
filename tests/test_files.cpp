/// \file
/// \brief Tests for glob matching and file enumeration.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Comprendo/Files.hpp"
#include "TempDir.hpp"

using Comprendo::FileSource;
using Comprendo::glob_match;

/// Globs follow the documented `*`, `?`, and `**` semantics.
TEST_CASE("glob matching", "[files]")
{
  CHECK(glob_match("build/**", "build/a.o"));
  CHECK(glob_match("build/**", "build/x/y/z.o"));
  CHECK_FALSE(glob_match("build/**", "src/build/a.o"));
  CHECK(glob_match("**/build/**", "src/build/a.o"));
  CHECK(glob_match("**/*.cpp", "main.cpp"));
  CHECK(glob_match("src/*.cpp", "src/main.cpp"));
  CHECK_FALSE(glob_match("src/*.cpp", "src/sub/main.cpp"));
  CHECK(glob_match("src/**/*.cpp", "src/sub/deep/main.cpp"));
  CHECK(glob_match("*.md", "docs/deep/notes.md"));
  CHECK_FALSE(glob_match("*.md", "docs/notes.mdx"));
  CHECK(glob_match("file?.h", "dir/file1.h"));
  CHECK_FALSE(glob_match("file?.h", "dir/file12.h"));
  CHECK(glob_match("a*b*c", "aXbYc"));
  CHECK_FALSE(glob_match("a*b*c", "aXbY"));
}

/// Outside a git work tree the directory walk applies roots and excludes.
TEST_CASE("enumeration walks a plain directory", "[files]")
{
  const Comprendo::Testing::TempDir dir {"files-walk"};
  for (const char *file :
       {"src/a.cpp", "src/sub/b.hpp", "lib/c.cpp", "build/gen.cpp", ".git/config"})
  {
    std::filesystem::create_directories((dir / file).parent_path());
    std::ofstream {dir / file} << "x\n";
  }

  const auto all = Comprendo::enumerate_files(dir.path(), {"."}, {"build/**"});
  REQUIRE(all.has_value());
  CHECK(all->source == FileSource::Walk);
  CHECK(all->files == std::vector<std::string> {"lib/c.cpp", "src/a.cpp", "src/sub/b.hpp"});

  const auto src = Comprendo::enumerate_files(dir.path(), {"src/"}, {"*.hpp"});
  REQUIRE(src.has_value());
  CHECK(src->files == std::vector<std::string> {"src/a.cpp"});

  CHECK_FALSE(Comprendo::enumerate_files(dir / "absent", {"."}, {}).has_value());
}

/// Inside a git work tree the listing comes from git and is relative to the root.
TEST_CASE("enumeration uses git inside a work tree", "[files]")
{
  const std::filesystem::path fixture = COMPRENDO_FIXTURE_DIR "/extract_basic";
  const auto listing = Comprendo::enumerate_files(fixture, {"."}, {});
  REQUIRE(listing.has_value());
  if (listing->source != FileSource::Git)
  {
    SKIP("fixture is not inside a git work tree");
  }
  CHECK(
    listing->files
    == std::vector<std::string> {
      "docs/notes.md",
      "include/geo/shape.hpp",
      "src/circle.cpp",
      "src/circle.hpp",
      "src/main.cpp",
      "src/util.py",
      "third_party/vendored.cpp",
    }
  );
}
