/// \file
/// \brief Tests for `#include` scanning and resolution.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <unordered_set>
#include <vector>

#include "Comprendo/Includes.hpp"

using Comprendo::IncludeDirective;
using Comprendo::resolve_include;
using Comprendo::scan_includes;

/// Directives are found with their line numbers and delimiter style.
TEST_CASE("scan finds include directives", "[includes]")
{
  const std::string source = "#include \"a.hpp\"\n"
                             "  #  include <vector>\n"
                             "\n"
                             "// #include \"commented.hpp\"\n"
                             "/* #include \"block.hpp\"\n"
                             "#include \"still-block.hpp\" */\n"
                             "#include MACRO_HEADER\n"
                             "#include \"\"\n"
                             "#includes \"nope.hpp\"\n"
                             "#include \"dir/b.h\" // trailing comment\n"
                             "#include \"last.hpp\"";
  const std::vector<IncludeDirective> expected = {
    {.line = 1, .target = "a.hpp", .angled = false},
    {.line = 2, .target = "vector", .angled = true},
    {.line = 10, .target = "dir/b.h", .angled = false},
    {.line = 11, .target = "last.hpp", .angled = false},
  };
  CHECK(scan_includes(source) == expected);
}

/// Resolution prefers the includer's directory, then the include paths in order.
TEST_CASE("resolve follows the include search order", "[includes]")
{
  const std::unordered_set<std::string> known
    = {"src/circle.hpp", "include/geo/shape.hpp", "src/geo/shape.hpp", "top.hpp"};
  const std::vector<std::string> paths = {"include", "src", "."};
  const auto resolve = [&](const char *includer, IncludeDirective directive)
  { return resolve_include(includer, directive, known, paths); };

  CHECK(resolve("src/main.cpp", {.target = "circle.hpp"}) == "src/circle.hpp");
  CHECK(resolve("src/main.cpp", {.target = "geo/shape.hpp"}) == "src/geo/shape.hpp");
  CHECK(
    resolve("src/main.cpp", {.target = "geo/shape.hpp", .angled = true}) == "include/geo/shape.hpp"
  );
  CHECK(resolve("lib/x.cpp", {.target = "geo/shape.hpp"}) == "include/geo/shape.hpp");
  CHECK(resolve("src/a/b.cpp", {.target = "../circle.hpp"}) == "src/circle.hpp");
  CHECK(resolve("src/main.cpp", {.target = "top.hpp"}) == "top.hpp");
  CHECK_FALSE(resolve("src/main.cpp", {.target = "vector", .angled = true}).has_value());
  CHECK_FALSE(resolve("main.cpp", {.target = "../outside.hpp"}).has_value());
}
