/// \file
/// \brief Tests for the ctags extractor: record parsing, kind mapping, chart building, fixtures.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Comprendo/Chart.hpp"
#include "Comprendo/ChartStore.hpp"
#include "Comprendo/CtagsExtractor.hpp"
#include "Comprendo/ExtractConfig.hpp"
#include "Comprendo/Process.hpp"
#include "Checks.hpp"
#include "TempDir.hpp"

namespace
{
  using Catch::Matchers::ContainsSubstring;
  using Comprendo::Chart;
  using Comprendo::CtagsSourceFile;
  using Comprendo::CtagsTag;
  using Comprendo::Edge;
  using Comprendo::EdgeKind;
  using Comprendo::Node;
  using Comprendo::NodeKind;
  using Comprendo::Provenance;
  using Comprendo::Testing::error_of;

  /// Find the node with a qualified name in a file.
  ///
  /// \param chart Chart to search.
  /// \param qualified_name Qualified name.
  /// \param file Repository-relative file.
  /// \return The node, or null.
  const Node *
  find_node(const Chart &chart, std::string_view qualified_name, std::string_view file)
  {
    const auto it = std::ranges::find_if(
      chart.nodes,
      [&](const Node &node) { return node.qualified_name == qualified_name && node.file == file; }
    );
    return it == chart.nodes.end() ? nullptr : &*it;
  }

  /// Find the node of a file.
  ///
  /// \param chart Chart to search.
  /// \param path Repository-relative file.
  /// \return The File node, or null.
  const Node *
  find_file(const Chart &chart, std::string_view path)
  {
    const Node *node = find_node(chart, path, path);
    return node != nullptr && node->kind == NodeKind::File ? node : nullptr;
  }

  /// Flatten a parse result to the tag it holds.
  ///
  /// \param parsed Result of Comprendo::parse_ctags_line().
  /// \return The tag, or nothing for errors and skipped records.
  std::optional<CtagsTag>
  parsed_tag(const std::expected<std::optional<CtagsTag>, std::string> &parsed)
  {
    return parsed.value_or(std::nullopt);
  }

  /// Look up the kind of a node.
  ///
  /// \param chart Chart to search.
  /// \param qualified_name Qualified name.
  /// \param file Repository-relative file.
  /// \return The node's kind, or nothing when the node does not exist.
  std::optional<NodeKind>
  kind_of(const Chart &chart, std::string_view qualified_name, std::string_view file)
  {
    const Node *node = find_node(chart, qualified_name, file);
    return node == nullptr ? std::nullopt : std::optional<NodeKind> {node->kind};
  }

  /// Find the unique `Contains` parent of a node.
  ///
  /// \param chart Chart to search.
  /// \param child Node whose parent is wanted; may be null.
  /// \return The parent, or null when \p child is null or has no unique parent.
  const Node *
  contains_parent(const Chart &chart, const Node *child)
  {
    if (child == nullptr)
    {
      return nullptr;
    }
    const Node *parent = nullptr;
    std::size_t parents = 0;
    for (const Edge &edge : chart.edges)
    {
      if (edge.kind == EdgeKind::Contains && edge.dst == child->id)
      {
        ++parents;
        const auto it = std::ranges::find_if(
          chart.nodes, [&](const Node &node) { return node.id == edge.src; }
        );
        parent = it == chart.nodes.end() ? nullptr : &*it;
      }
    }
    return parents == 1 ? parent : nullptr;
  }

  /// Check for an `Includes` edge between two files.
  ///
  /// \param chart Chart to search.
  /// \param from Including file.
  /// \param to Included file.
  /// \return True when the edge exists.
  bool
  includes(const Chart &chart, std::string_view from, std::string_view to)
  {
    const Node *src = find_file(chart, from);
    const Node *dst = find_file(chart, to);
    return src != nullptr && dst != nullptr
      && std::ranges::any_of(
             chart.edges,
             [&](const Edge &edge)
             {
               return edge.kind == EdgeKind::Includes && edge.src == src->id && edge.dst == dst->id;
             }
      );
  }

  /// Check whether Universal Ctags is available on PATH.
  ///
  /// \return True when `ctags --version` reports Universal Ctags.
  bool
  have_universal_ctags()
  {
    const auto version = Comprendo::run_process({"ctags", "--version"});
    return version && version->exit_code == 0 && version->out.contains("Universal Ctags");
  }

  /// Copy the basic fixture into a scratch directory outside any git work tree.
  ///
  /// \param dir Scratch directory.
  /// \param config_toml Contents of `.comprendo/config.toml`.
  /// \return Extractor configuration for the copy, or nothing when loading failed.
  std::optional<Comprendo::ExtractConfig>
  prepare_fixture(const Comprendo::Testing::TempDir &dir, std::string_view config_toml)
  {
    std::filesystem::copy(
      COMPRENDO_FIXTURE_DIR "/extract_basic", dir.path(), std::filesystem::copy_options::recursive
    );
    std::filesystem::create_directories(dir / ".comprendo");
    std::ofstream {dir / ".comprendo/config.toml"} << config_toml;
    auto config = Comprendo::load_extract_config(dir.path());
    CHECK(config.has_value());
    if (!config)
    {
      return std::nullopt;
    }
    return *config;
  }
} // namespace

/// Every tag reaches the chart through parse_ctags_line(), so each JSON field must
/// land in the right CtagsTag member. Parses a complete ctags record and compares
/// the whole struct, checks pseudo-tag records are skipped, checks malformed JSON,
/// non-objects, and records without a name are errors, and checks that a `= delete`
/// pattern sets the deleted flag while an ordinary pattern does not.
TEST_CASE("ctags line parsing", "[ctags]")
{
  const auto tag = Comprendo::parse_ctags_line(
    R"({"_type": "tag", "name": "area", "path": "src/circle.cpp", "pattern": "/^x$/", "line": 10,)"
    R"( "typeref": "typename:double", "kind": "function", "signature": "() const",)"
    R"( "scope": "geo::Circle", "scopeKind": "class", "end": 13})"
  );
  CHECK(
    parsed_tag(tag)
    == CtagsTag {
      .name = "area",
      .path = "src/circle.cpp",
      .kind = "function",
      .scope = "geo::Circle",
      .scope_kind = "class",
      .signature = "() const",
      .line = 10,
      .end = 13,
    }
  );

  const auto pseudo
    = Comprendo::parse_ctags_line(R"({"_type": "ptag", "name": "JSON_OUTPUT_VERSION"})");
  CHECK(pseudo.has_value());
  CHECK_FALSE(parsed_tag(pseudo).has_value());

  CHECK_THAT(error_of(Comprendo::parse_ctags_line("{oops")), ContainsSubstring("bad ctags output"));
  CHECK_THAT(error_of(Comprendo::parse_ctags_line("[1]")), ContainsSubstring("not an object"));
  CHECK_THAT(
    error_of(Comprendo::parse_ctags_line(R"({"_type": "tag", "path": "a.c", "kind": "function"})")),
    ContainsSubstring("lacks name")
  );

  const auto deleted = Comprendo::parse_ctags_line(
    R"json({"_type": "tag", "name": "T", "path": "t.hpp", "pattern": "/^  T(const T &) = delete;$/",)json"
    R"json( "line": 3, "kind": "prototype", "signature": "(const T &)"})json"
  );
  CHECK(
    parsed_tag(deleted)
    == CtagsTag {
      .name = "T",
      .path = "t.hpp",
      .kind = "prototype",
      .signature = "(const T &)",
      .line = 3,
      .deleted = true,
    }
  );
}

/// ctags kind names differ between languages, and the node kind drives colors,
/// grouping, and id computation. Checks representative mappings: functions become
/// methods only inside class-like scopes, Python `member` tags are methods,
/// prototypes map like functions, unknown kinds become Other, and locals, include
/// references, and similar kinds are not charted.
TEST_CASE("ctags kind mapping", "[ctags]")
{
  using Comprendo::map_ctags_kind;
  CHECK(map_ctags_kind("C++", "function", "") == NodeKind::Function);
  CHECK(map_ctags_kind("C++", "function", "namespace") == NodeKind::Function);
  CHECK(map_ctags_kind("C++", "function", "class") == NodeKind::Method);
  CHECK(map_ctags_kind("C++", "function", "struct") == NodeKind::Method);
  CHECK(map_ctags_kind("C++", "member", "class") == NodeKind::Field);
  CHECK(map_ctags_kind("Python", "member", "class") == NodeKind::Method);
  CHECK(map_ctags_kind("C++", "typedef", "") == NodeKind::TypeAlias);
  CHECK(map_ctags_kind("C", "macro", "") == NodeKind::Macro);
  CHECK(map_ctags_kind("C++", "enumerator", "enum") == NodeKind::Field);
  CHECK(map_ctags_kind("Go", "package", "") == NodeKind::Module);
  CHECK(map_ctags_kind("Rust", "somethingNew", "") == NodeKind::Other);
  CHECK(map_ctags_kind("C++", "prototype", "class") == NodeKind::Method);
  CHECK(map_ctags_kind("C++", "prototype", "namespace") == NodeKind::Function);
  CHECK_FALSE(map_ctags_kind("C++", "local", "function").has_value());
  CHECK_FALSE(map_ctags_kind("C", "header", "").has_value());
}

/// Inheritance must be charted even without clangd (files outside the build,
/// ctags-only extraction), from the base classes ctags reports. Parses records
/// whose `inherits` field has access specifiers, templates with commas, and
/// qualified names, then builds a chart and checks bases resolve through the
/// enclosing scopes while standard-library and ambiguous bases are skipped.
TEST_CASE("base classes become inheritance edges", "[ctags]")
{
  const auto tag = Comprendo::parse_ctags_line(
    R"({"_type": "tag", "name": "D", "path": "a.hpp", "line": 3, "kind": "class",)"
    R"( "inherits": "public Base<int, char>, virtual geo::Named,std::exception"})"
  );
  const auto parsed = parsed_tag(tag);
  CHECK(parsed.has_value());
  if (parsed)
  {
    CHECK(parsed->bases == std::vector<std::string> {"Base", "geo::Named", "std::exception"});
  }

  const std::vector<CtagsSourceFile> files = {{.path = "a.hpp", .language = "C++", .lines = 40}};
  const auto type =
    [](const char *name, const char *scope, std::uint32_t line, std::vector<std::string> bases = {})
  {
    return CtagsTag {
      .name = name,
      .path = "a.hpp",
      .kind = "class",
      .scope = scope,
      .scope_kind = *scope == '\0' ? "" : "namespace",
      .line = line,
      .end = line + 1,
      .bases = std::move(bases),
    };
  };
  const std::vector<CtagsTag> tags = {
    type("Shape", "geo", 1),
    type("Shape", "", 3),
    type("Twin", "x", 5),
    type("Twin", "y", 7),
    type("Circle", "geo", 9, {"Shape", "std::exception"}),
    type("Square", "", 11, {"Shape"}),
    type("Odd", "", 13, {"Twin"}),
  };
  const Chart chart = Comprendo::build_ctags_chart(files, tags, {"."});
  CHECK(Comprendo::validate(chart).ok());
  const auto inherits = [&](const char *derived, const char *base)
  {
    const auto id = [&](const char *name)
    {
      const auto node = std::ranges::find(chart.nodes, std::string {name}, &Node::qualified_name);
      return node == chart.nodes.end() ? 0 : node->id;
    };
    return std::ranges::any_of(
      chart.edges,
      [&](const Edge &edge)
      { return edge.kind == EdgeKind::Inherits && edge.src == id(derived) && edge.dst == id(base); }
    );
  };
  CHECK(inherits("geo::Circle", "geo::Shape"));
  CHECK(inherits("Square", "Shape"));
  CHECK(std::ranges::count(chart.edges, EdgeKind::Inherits, &Edge::kind) == 2);
}

/// build_ctags_chart() is pure, so its rules can be tested exactly without ctags.
/// Feeds two files and hand-written tags in scrambled order (an out-of-line method
/// whose class lives in another file, a duplicated namespace tag, a tag in an
/// unknown file, an unmatched declaration) and checks node/edge counts, kinds,
/// ids, cross-file containment, includes, and that reversed input gives the same chart.
TEST_CASE("chart building from tags", "[ctags]")
{
  const std::vector<CtagsSourceFile> files = {
    {.path = "src/b.cpp",
     .language = "C++",
     .lines = 20,
     .includes = {{.line = 1, .target = "a.hpp"}}},
    {.path = "src/a.hpp", .language = "C++", .lines = 10},
  };
  const std::vector<CtagsTag> tags = {
    {.name = "run",
     .path = "src/b.cpp",
     .kind = "function",
     .scope = "ns::A",
     .scope_kind = "class",
     .signature = "()",
     .line = 5,
     .end = 9},
    {.name = "A",
     .path = "src/a.hpp",
     .kind = "class",
     .scope = "ns",
     .scope_kind = "namespace",
     .line = 3,
     .end = 8},
    {.name = "ns", .path = "src/a.hpp", .kind = "namespace", .line = 1, .end = 10},
    {.name = "ns", .path = "src/b.cpp", .kind = "namespace", .line = 3, .end = 20},
    {.name = "ns", .path = "src/b.cpp", .kind = "namespace", .line = 3, .end = 20},
    {.name = "ghost", .path = "src/unknown.cpp", .kind = "function", .line = 1},
    {.name = "decl", .path = "src/a.hpp", .kind = "prototype", .line = 4},
  };

  const Chart chart = Comprendo::build_ctags_chart(files, tags, {"."});
  CHECK(Comprendo::validate(chart).ok());
  CHECK(chart.nodes.size() == 2 + 5);
  CHECK(chart.edges.size() == 5 + 1);
  CHECK(find_node(chart, "decl", "src/a.hpp") != nullptr);
  CHECK(
    std::ranges::all_of(
      chart.nodes, [](const Node &n) { return n.provenance == Provenance::Extractor; }
    )
  );

  const Node *run = find_node(chart, "ns::A::run", "src/b.cpp");
  CHECK(run != nullptr);
  if (run == nullptr)
  {
    return;
  }
  CHECK(run->kind == NodeKind::Method);
  CHECK(run->loc == 5);
  CHECK(run->id == Comprendo::make_node_id(NodeKind::Method, "ns::A::run", "src/b.cpp", "()"));
  const Node *run_parent = contains_parent(chart, run);
  CHECK(run_parent != nullptr);
  if (run_parent == nullptr)
  {
    return;
  }
  CHECK(run_parent->qualified_name == "ns::A");
  CHECK(run_parent->file == "src/a.hpp");

  const Node *ns_b = find_node(chart, "ns", "src/b.cpp");
  CHECK(ns_b != nullptr);
  CHECK(contains_parent(chart, ns_b) == find_file(chart, "src/b.cpp"));

  const Node *b_file = find_file(chart, "src/b.cpp");
  CHECK(b_file != nullptr);
  if (b_file == nullptr)
  {
    return;
  }
  CHECK(b_file->name == "b.cpp");
  CHECK(b_file->loc == 20);
  CHECK(includes(chart, "src/b.cpp", "src/a.hpp"));

  const Chart again
    = Comprendo::build_ctags_chart({files[1], files[0]}, {tags.rbegin(), tags.rend()}, {"."});
  CHECK(again == chart);
}

/// Declarations must be charted only when no definition exists, or every function
/// would appear twice. Builds definitions and prototypes covering each matching
/// tier (exact signature, renamed parameters with defaults, unique arity), a
/// qualifier mismatch, a pure virtual, a `(void)` declaration, a deleted
/// constructor, and a lambda, then checks exactly the expected ones become nodes.
TEST_CASE("declarations are matched to definitions", "[ctags]")
{
  const std::vector<CtagsSourceFile> files = {
    {.path = "a.cpp", .language = "C++", .lines = 50},
    {.path = "a.hpp", .language = "C++", .lines = 30},
  };
  const auto def
    = [](const char *name, const char *scope, const char *signature, std::uint32_t line)
  {
    return CtagsTag {
      .name = name,
      .path = "a.cpp",
      .kind = "function",
      .scope = scope,
      .scope_kind = *scope == '\0' ? "" : "class",
      .signature = signature,
      .line = line,
      .end = line + 1
    };
  };
  const auto decl
    = [](const char *name, const char *scope, const char *signature, std::uint32_t line)
  {
    return CtagsTag {
      .name = name,
      .path = "a.hpp",
      .kind = "prototype",
      .scope = scope,
      .scope_kind = *scope == '\0' ? "" : "class",
      .signature = signature,
      .line = line,
      .end = line
    };
  };
  std::vector<CtagsTag> tags = {
    {.name = "S", .path = "a.hpp", .kind = "class", .line = 1, .end = 20},
    def("f", "", "(int value)", 1),
    def("m", "S", "(int x) const", 5),
    def("m", "S", "(double x) const", 9),
    def("h", "", "(Gadget g)", 13),
    decl("f", "", "(int value)", 21),
    decl("m", "S", "(int) const", 3),
    decl("m", "S", "(double y = 2.0) const", 4),
    decl("h", "", "(Widget w)", 22),
    decl("pure", "S", "() const", 5),
    decl("m", "S", "(int) &&", 6),
    decl("ghost", "", "(void)", 23),
    def("__anon1a2b", "", "(int a)", 30),
  };
  CtagsTag deleted = decl("S", "S", "(const S &)", 7);
  deleted.deleted = true;
  tags.push_back(deleted);

  const Chart chart = Comprendo::build_ctags_chart(files, tags, {});
  CHECK(Comprendo::validate(chart).ok());

  const auto declared_in_header = [&](std::string_view qualified_name)
  {
    return std::ranges::count_if(
      chart.nodes,
      [&](const Node &node)
      { return node.qualified_name == qualified_name && node.file == "a.hpp"; }
    );
  };
  CHECK(declared_in_header("f") == 0);
  CHECK(declared_in_header("h") == 0);
  CHECK(declared_in_header("S::S") == 0);
  CHECK(declared_in_header("ghost") == 1);
  CHECK(find_node(chart, "__anon1a2b", "a.cpp") == nullptr);
  CHECK(declared_in_header("S::m") == 1);
  const Node *pure = find_node(chart, "S::pure", "a.hpp");
  CHECK(kind_of(chart, "S::pure", "a.hpp") == NodeKind::Method);
  CHECK(contains_parent(chart, pure) == find_node(chart, "S", "a.hpp"));
  const Node *rvalue = find_node(chart, "S::m", "a.hpp");
  CHECK(rvalue != nullptr);
  if (rvalue == nullptr)
  {
    return;
  }
  CHECK(rvalue->signature == "(int) &&");
}

/// End-to-end check of the real pipeline (file listing, language detection, ctags
/// in batches, include scanning, chart building) on a small C++/Python tree.
/// Copies the fixture outside git, configures excludes, languages, and a batch size
/// of 2 to force several ctags calls, then checks totals, kinds, containment,
/// declarations, includes, header fields, determinism, and SQLite round-trip.
TEST_CASE("ctags extraction of the basic fixture", "[ctags][fixture]")
{
  if (!have_universal_ctags())
  {
    SKIP("Universal Ctags is not installed");
  }
  const Comprendo::Testing::TempDir dir {"ctags-basic"};
  const auto config = prepare_fixture(
    dir,
    "[extract]\nexcludes = [\"third_party/**\"]\nlanguages = [\"C++\", \"Python\"]\n"
    "ctags_batch = 2\n"
  );

  if (!config)
  {
    return;
  }

  Comprendo::CtagsExtractor extractor;
  const auto chart = extractor.extract(*config);
  CHECK(chart.has_value());
  if (!chart)
  {
    return;
  }
  CHECK(Comprendo::validate(*chart).ok());

  CHECK(chart->nodes.size() == 31);
  CHECK(chart->edges.size() == 30);
  const Node *circle = find_node(*chart, "geo::Circle", "src/circle.hpp");
  const Node *shape = find_node(*chart, "geo::Shape", "include/geo/shape.hpp");
  CHECK(circle != nullptr);
  CHECK(shape != nullptr);
  if (circle != nullptr && shape != nullptr)
  {
    CHECK(
      std::ranges::any_of(
        chart->edges,
        [&](const Edge &edge)
        {
          return edge.kind == EdgeKind::Inherits && edge.src == circle->id && edge.dst == shape->id;
        }
      )
    );
  }
  CHECK(find_file(*chart, "docs/notes.md") == nullptr);
  CHECK(find_node(*chart, "vendored", "third_party/vendored.cpp") == nullptr);

  SECTION("symbols carry kinds and lines")
  {
    const Node *area = find_node(*chart, "geo::Circle::area", "src/circle.cpp");
    CHECK(area != nullptr);
    if (area == nullptr)
    {
      return;
    }
    CHECK(area->kind == NodeKind::Method);
    CHECK(area->line_start == 10);
    CHECK(area->line_end == 13);
    CHECK(area->signature == "() const");
    CHECK(kind_of(*chart, "SCALE_FACTOR", "src/main.cpp") == NodeKind::Macro);
    CHECK(kind_of(*chart, "Number", "src/main.cpp") == NodeKind::TypeAlias);
    CHECK(kind_of(*chart, "geo::Point", "include/geo/shape.hpp") == NodeKind::Struct);
    CHECK(kind_of(*chart, "geo::Point::x", "include/geo/shape.hpp") == NodeKind::Field);
    CHECK(kind_of(*chart, "Helper.run", "src/util.py") == NodeKind::Method);
  }

  SECTION("declarations without a definition are charted once")
  {
    const auto count = [&](std::string_view qualified_name)
    {
      return std::ranges::count_if(
        chart->nodes, [&](const Node &node) { return node.qualified_name == qualified_name; }
      );
    };
    const Node *pure = find_node(*chart, "geo::Shape::area", "include/geo/shape.hpp");
    CHECK(pure != nullptr);
    CHECK(kind_of(*chart, "geo::Shape::area", "include/geo/shape.hpp") == NodeKind::Method);
    CHECK(
      contains_parent(*chart, pure) == find_node(*chart, "geo::Shape", "include/geo/shape.hpp")
    );
    CHECK(find_node(*chart, "geo::Shape::~Shape", "include/geo/shape.hpp") != nullptr);
    const Node *declared = find_node(*chart, "log_value", "src/main.cpp");
    CHECK(declared != nullptr);
    CHECK(contains_parent(*chart, declared) == find_file(*chart, "src/main.cpp"));
    CHECK(count("geo::distance") == 1);
    CHECK(count("geo::perimeter") == 1);
    CHECK(find_node(*chart, "geo::perimeter", "src/circle.cpp") != nullptr);
    CHECK(count("geo::Circle::Circle") == 1);
    CHECK(count("geo::Circle::area") == 1);
  }

  SECTION("overloads are distinct nodes")
  {
    const auto scales = std::ranges::count_if(
      chart->nodes, [](const Node &node) { return node.qualified_name == "scale"; }
    );
    CHECK(scales == 2);
  }

  SECTION("containment resolves scopes")
  {
    const Node *area = find_node(*chart, "geo::Circle::area", "src/circle.cpp");
    CHECK(area != nullptr);
    CHECK(contains_parent(*chart, area) == find_node(*chart, "geo::Circle", "src/circle.hpp"));
    const Node *distance = find_node(*chart, "geo::distance", "src/circle.cpp");
    CHECK(distance != nullptr);
    CHECK(contains_parent(*chart, distance) == find_node(*chart, "geo", "src/circle.cpp"));
    const Node *run = find_node(*chart, "Helper.run", "src/util.py");
    CHECK(run != nullptr);
    CHECK(contains_parent(*chart, run) == find_node(*chart, "Helper", "src/util.py"));
    const Node *main_function = find_node(*chart, "main", "src/main.cpp");
    CHECK(main_function != nullptr);
    CHECK(contains_parent(*chart, main_function) == find_file(*chart, "src/main.cpp"));
  }

  SECTION("includes resolve to project files only")
  {
    CHECK(includes(*chart, "src/circle.hpp", "include/geo/shape.hpp"));
    CHECK(includes(*chart, "src/circle.cpp", "src/circle.hpp"));
    CHECK(includes(*chart, "src/main.cpp", "src/circle.hpp"));
    CHECK_FALSE(includes(*chart, "src/main.cpp", "include/geo/shape.hpp"));
  }

  SECTION("header fields describe the extraction")
  {
    CHECK(chart->project == "ctags-basic");
    CHECK(chart->source_root == std::filesystem::weakly_canonical(dir.path()).string());
    CHECK_THAT(chart->generator, ContainsSubstring("(ctags)"));
    CHECK(chart->generated_at.size() == std::string_view {"2026-01-01T00:00:00Z"}.size());
  }

  SECTION("extraction is deterministic and storable")
  {
    auto again = extractor.extract(*config);
    CHECK(again.has_value());
    if (!again)
    {
      return;
    }
    again->generated_at = chart->generated_at;
    CHECK(*again == *chart);

    CHECK(Comprendo::write_chart(dir / "chart.sqlite", *chart).has_value());
    const auto read = Comprendo::read_chart(dir / "chart.sqlite");
    CHECK(read == *chart);
  }
}

/// A missing or wrong ctags (for example Exuberant Ctags, which lacks JSON output)
/// must produce a clear error instead of a crash or an empty chart. Runs the
/// extractor with a nonexistent program name and with `true`, which runs but is
/// not Universal Ctags, and checks both error messages.
TEST_CASE("ctags extraction reports a bad executable", "[ctags]")
{
  const Comprendo::Testing::TempDir dir {"ctags-bad"};
  Comprendo::ExtractConfig config;
  config.root = dir.path();

  config.ctags = "comprendo-no-such-ctags";
  const auto missing = Comprendo::CtagsExtractor {}.extract(config);
  CHECK_FALSE(missing.has_value());
  CHECK_THAT(error_of(missing), ContainsSubstring("cannot run 'comprendo-no-such-ctags'"));

  config.ctags = "true";
  const auto wrong = Comprendo::CtagsExtractor {}.extract(config);
  CHECK_FALSE(wrong.has_value());
  CHECK_THAT(error_of(wrong), ContainsSubstring("is not Universal Ctags"));
}
