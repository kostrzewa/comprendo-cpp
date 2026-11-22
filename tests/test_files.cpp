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

/// Exclude patterns decide which files are extracted, so the documented glob
/// semantics must hold. Checks `**` spanning zero or more directories, `*` staying
/// within one segment, slash-free patterns matching the file name at any depth,
/// `?` matching exactly one character, and backtracking across several `*`.
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

/// Trees outside git (tarballs, copies) are listed by walking directories, which
/// must apply roots and excludes and skip `.git`. Creates files in a scratch
/// directory, including `build/` and `.git/` entries, then checks the full
/// listing, a listing restricted to `src/` with a `*.hpp` exclude, and the error
/// for a missing root.
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
  CHECK(all.has_value());
  if (!all)
  {
    return;
  }
  CHECK(all->source == FileSource::Walk);
  CHECK(all->files == std::vector<std::string> {"lib/c.cpp", "src/a.cpp", "src/sub/b.hpp"});

  const auto src = Comprendo::enumerate_files(dir.path(), {"src/"}, {"*.hpp"});
  CHECK(src.has_value());
  if (!src)
  {
    return;
  }
  CHECK(src->files == std::vector<std::string> {"src/a.cpp"});

  CHECK_FALSE(Comprendo::enumerate_files(dir / "absent", {"."}, {}).has_value());
}

/// Inside a work tree, git's view (tracked plus untracked, honouring .gitignore)
/// is authoritative, and paths must be relative to the requested root even when
/// it is a subdirectory of the repository. Lists the in-repo fixture and checks
/// the exact sorted file list; skipped when the fixture is not inside git.
TEST_CASE("enumeration uses git inside a work tree", "[files]")
{
  const std::filesystem::path fixture = COMPRENDO_FIXTURE_DIR "/extract_basic";
  const auto listing = Comprendo::enumerate_files(fixture, {"."}, {});
  CHECK(listing.has_value());
  if (!listing)
  {
    return;
  }
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
