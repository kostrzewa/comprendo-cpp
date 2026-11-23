/// \file
/// \brief Tests for the clangd backend: position helpers, node mapping, and extraction.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "Comprendo/Chart.hpp"
#include "Comprendo/ClangdExtractor.hpp"
#include "Comprendo/ExtractConfig.hpp"
#include "Comprendo/Process.hpp"
#include "Checks.hpp"
#include "TempDir.hpp"

using Catch::Matchers::ContainsSubstring;
using Comprendo::Chart;
using Comprendo::Edge;
using Comprendo::EdgeKind;
using Comprendo::Node;
using Comprendo::NodeKind;

namespace
{
  /// Kinds accepted as call targets.
  constexpr std::array<NodeKind, 2> kCallables = {NodeKind::Function, NodeKind::Method};

  /// Check whether a program runs and exits successfully with `--version`.
  ///
  /// \param program Executable.
  /// \return True when available.
  bool
  have_program(const std::string &program)
  {
    const auto version = Comprendo::run_process({program, "--version"});
    return version && version->exit_code == 0;
  }

  /// Build a node for locator tests.
  ///
  /// \param kind Node kind.
  /// \param qualified_name Qualified name.
  /// \param file Repository-relative file.
  /// \param first First line.
  /// \param last Last line.
  /// \return The node.
  Node
  make_node(
    NodeKind kind,
    std::string qualified_name,
    std::string file,
    std::uint32_t first,
    std::uint32_t last
  )
  {
    Node node;
    node.kind = kind;
    node.name = qualified_name;
    node.qualified_name = std::move(qualified_name);
    node.file = std::move(file);
    node.line_start = first;
    node.line_end = last;
    return node;
  }

  /// Find an edge by the qualified names and signatures of its ends.
  ///
  /// \param chart Chart to search.
  /// \param kind Edge kind.
  /// \param from Source as "qualified_name signature".
  /// \param to Destination as "qualified_name signature".
  /// \return The edge, or null.
  const Edge *
  find_edge(const Chart &chart, EdgeKind kind, std::string_view from, std::string_view to)
  {
    const auto label = [&](std::uint64_t id)
    {
      const auto node = std::ranges::find(chart.nodes, id, &Node::id);
      return node == chart.nodes.end() ? std::string {}
                                       : node->qualified_name + " " + node->signature;
    };
    const auto edge = std::ranges::find_if(
      chart.edges,
      [&](const Edge &candidate)
      {
        return candidate.kind == kind && label(candidate.src) == from && label(candidate.dst) == to;
      }
    );
    return edge == chart.edges.end() ? nullptr : &*edge;
  }

  /// Copy the basic fixture and write a configuration excluding its vendored code.
  ///
  /// \param dir Scratch directory receiving the copy.
  /// \return Configuration for the copy, or nothing when loading failed.
  std::optional<Comprendo::ExtractConfig>
  prepare_fixture(const Comprendo::Testing::TempDir &dir)
  {
    std::filesystem::copy(
      COMPRENDO_FIXTURE_DIR "/extract_basic", dir.path(), std::filesystem::copy_options::recursive
    );
    std::filesystem::create_directories(dir / ".comprendo");
    std::ofstream {dir / ".comprendo/config.toml"} << "[extract]\nexcludes = "
                                                      "[\"third_party/**\"]\n";
    auto config = Comprendo::load_extract_config(dir.path());
    CHECK(config.has_value());
    return config ? std::optional {*config} : std::nullopt;
  }

  /// Write a compilation database for the fixture's sources into a directory.
  ///
  /// \param root Fixture copy.
  /// \param dir Directory receiving `compile_commands.json`.
  void
  write_compile_commands(const std::filesystem::path &root, const std::filesystem::path &dir)
  {
    std::filesystem::create_directories(dir);
    std::ofstream out {dir / "compile_commands.json"};
    out << "[";
    const char *separator = "";
    for (const char *file : {"src/main.cpp", "src/circle.cpp"})
    {
      out << separator << R"({"directory": ")" << root.string() << R"(", "file": ")" << file
          << R"(", "arguments": ["clang++", "-std=c++20", "-Iinclude", "-Isrc", "-c", ")" << file
          << R"("]})";
      separator = ",";
    }
    out << "]";
  }
} // namespace

/// LSP requests address a symbol by position, but ctags only gives its line, so
/// the column must be found in the line text without matching substrings of other
/// identifiers. Checks plain functions, qualified constructors (the occurrence
/// followed by '(' wins), destructors, operators, words containing the name, a
/// fallback to the first occurrence, and a missing name.
TEST_CASE("name columns are found in declaration lines", "[clangd]")
{
  using Comprendo::find_name_column;
  CHECK(find_name_column("double scale(double value)", "scale") == std::optional<std::size_t> {7});
  CHECK(find_name_column("Circle::Circle (double r)", "Circle") == std::optional<std::size_t> {8});
  CHECK(
    find_name_column("  virtual ~Shape() = default;", "~Shape") == std::optional<std::size_t> {10}
  );
  CHECK(
    find_name_column("bool operator==(const A &)", "operator==") == std::optional<std::size_t> {5}
  );
  CHECK(find_name_column("int areas; int area() const", "area") == std::optional<std::size_t> {15});
  CHECK(
    find_name_column("class Circle final : public Shape", "Circle")
    == std::optional<std::size_t> {6}
  );
  CHECK_FALSE(find_name_column("int areas;", "area").has_value());
  CHECK_FALSE(find_name_column("int x;", "").has_value());
}

/// clangd needs the compilation database, which usually lives in a build
/// directory whose name varies per project. Builds scratch trees and checks the
/// search order: an explicit directory (present or not), then the root, then
/// `build/`, then the first subdirectory of `build/` by name.
TEST_CASE("compilation databases are discovered", "[clangd]")
{
  const Comprendo::Testing::TempDir dir {"clangd-compile-commands"};
  const auto touch = [&](std::string_view sub)
  {
    std::filesystem::create_directories(dir / sub);
    std::ofstream {dir / sub / "compile_commands.json"} << "[]";
  };
  using Comprendo::find_compile_commands;
  CHECK_FALSE(find_compile_commands(dir.path(), "").has_value());

  touch("build/release");
  touch("build/debug");
  CHECK(find_compile_commands(dir.path(), "") == std::optional {dir / "build/debug"});
  touch("build");
  CHECK(find_compile_commands(dir.path(), "") == std::optional {dir / "build"});
  touch(".");
  CHECK(find_compile_commands(dir.path(), "") == std::optional {dir.path()});

  CHECK(
    find_compile_commands(dir.path(), "build/release") == std::optional {dir / "build/release"}
  );
  CHECK_FALSE(find_compile_commands(dir.path(), "out").has_value());
}

/// Call and type hierarchy results name a file and line, which must map to the
/// innermost matching chart node, falling back to the qualified name only when it
/// is unambiguous. Builds a chart with nested callables, a class, and duplicate
/// names, then checks innermost selection, kind filtering, the name fallback, and
/// that ambiguous names resolve to nothing.
TEST_CASE("node locator maps positions to nodes", "[clangd]")
{
  Chart chart;
  chart.nodes = {
    make_node(NodeKind::Class, "C", "a.cpp", 1, 30),
    make_node(NodeKind::Method, "C::f", "a.cpp", 10, 20),
    make_node(NodeKind::Function, "C::f::inner", "a.cpp", 12, 14),
    make_node(NodeKind::Function, "h", "b.hpp", 3, 3),
    make_node(NodeKind::Function, "twice", "c.cpp", 1, 1),
    make_node(NodeKind::Function, "twice", "d.cpp", 1, 1),
  };
  const Comprendo::NodeLocator locator {chart};
  CHECK(locator.locate("a.cpp", 13, "", kCallables) == std::optional<std::size_t> {2});
  CHECK(locator.locate("a.cpp", 11, "", kCallables) == std::optional<std::size_t> {1});
  CHECK_FALSE(locator.locate("a.cpp", 25, "", kCallables).has_value());
  const std::array classes = {NodeKind::Class};
  CHECK(locator.locate("a.cpp", 13, "", classes) == std::optional<std::size_t> {0});
  CHECK(locator.locate("/usr/include/x.h", 9, "h", kCallables) == std::optional<std::size_t> {3});
  CHECK_FALSE(locator.locate("/usr/include/x.h", 9, "twice", kCallables).has_value());
  CHECK_FALSE(locator.locate("/usr/include/x.h", 9, "h", classes).has_value());
}

/// The default `auto` backend must degrade to the ctags chart when clangd cannot
/// run, while an explicit `clangd` backend must fail loudly. Extracts the fixture
/// without a compilation database, then with one but an unusable clangd binary,
/// and checks the fallback has no call edges and the required run reports why.
TEST_CASE("clangd backend falls back to ctags only when optional", "[clangd][fixture]")
{
  if (!have_program("ctags"))
  {
    SKIP("Universal Ctags is not installed");
  }
  const Comprendo::Testing::TempDir dir {"clangd-fallback"};
  auto config = prepare_fixture(dir);
  if (!config)
  {
    return;
  }
  const auto fallback = Comprendo::ClangdExtractor {false}.extract(*config);
  CHECK(fallback.has_value());
  if (fallback)
  {
    CHECK(
      std::ranges::none_of(
        fallback->edges, [](const Edge &edge) { return edge.kind == EdgeKind::Calls; }
      )
    );
  }
  CHECK_THAT(
    Comprendo::Testing::error_of(Comprendo::ClangdExtractor {true}.extract(*config)),
    ContainsSubstring("compile_commands.json")
  );

  write_compile_commands(dir.path(), dir.path());
  config->clangd = (dir / "no-such-clangd").string();
  CHECK_THAT(
    Comprendo::Testing::error_of(Comprendo::ClangdExtractor {true}.extract(*config)),
    ContainsSubstring("cannot run clangd")
  );
}

/// The point of the clangd backend is resolving what ctags cannot: overloads,
/// calls through a base reference, inheritance, and uses of types and fields.
/// Extracts the fixture with real clangd-22, then checks main calls the double
/// overload of scale but not the int one, total calls the virtual Shape::area
/// with the call site as evidence, Circle inherits Shape, and the use edges
/// (without a method "using" its own class or a base specifier counted twice).
TEST_CASE("clangd resolves calls and inheritance in the basic fixture", "[clangd][fixture]")
{
  if (!have_program("ctags") || !have_program("clangd-22"))
  {
    SKIP("Universal Ctags or clangd-22 is not installed");
  }
  const Comprendo::Testing::TempDir dir {"clangd-basic"};
  const auto config = prepare_fixture(dir);
  if (!config)
  {
    return;
  }
  write_compile_commands(dir.path(), dir / "build/debug");

  const auto chart = Comprendo::ClangdExtractor {true}.extract(*config);
  CHECK(chart.has_value());
  if (!chart)
  {
    return;
  }
  CHECK(Comprendo::validate(*chart).ok());
  CHECK(find_edge(*chart, EdgeKind::Calls, "main ()", "scale (double value)") != nullptr);
  CHECK(find_edge(*chart, EdgeKind::Calls, "main ()", "scale (int value)") == nullptr);
  CHECK(
    find_edge(*chart, EdgeKind::Calls, "main ()", "total (const geo::Shape & shape)") != nullptr
  );
  CHECK(
    find_edge(*chart, EdgeKind::Calls, "main ()", "geo::Circle::Circle (double radius)") != nullptr
  );
  const Edge *virtual_call = find_edge(
    *chart, EdgeKind::Calls, "total (const geo::Shape & shape)", "geo::Shape::area () const"
  );
  CHECK(virtual_call != nullptr);
  if (virtual_call != nullptr)
  {
    CHECK(virtual_call->evidence == "src/main.cpp:27");
    CHECK(virtual_call->provenance == Comprendo::Provenance::Extractor);
  }
  CHECK(find_edge(*chart, EdgeKind::Inherits, "geo::Circle ", "geo::Shape ") != nullptr);
  CHECK(find_edge(*chart, EdgeKind::Uses, "geo::Circle ", "geo::Shape ") == nullptr);
  CHECK(
    find_edge(*chart, EdgeKind::Uses, "geo::Circle::area () const", "geo::Circle::radius_ ")
    != nullptr
  );
  CHECK(find_edge(*chart, EdgeKind::Uses, "geo::Circle::area () const", "geo::Circle ") == nullptr);
  CHECK(find_edge(*chart, EdgeKind::Uses, "main ()", "geo::Circle ") != nullptr);
  const Edge *field_use = find_edge(
    *chart, EdgeKind::Uses, "geo::distance (const Point & a,const Point & b)", "geo::Point::x "
  );
  CHECK(field_use != nullptr);
  if (field_use != nullptr)
  {
    CHECK(field_use->weight == 2.0F);
  }
  CHECK(chart->generator.contains("clangd"));
}
