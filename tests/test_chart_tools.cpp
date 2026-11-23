/// \file
/// \brief Tests for the chart tools behind `/chart`, run against scratch projects.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "Comprendo/Chart.hpp"
#include "Comprendo/ChartStore.hpp"
#include "Comprendo/ChartTools.hpp"
#include "Comprendo/Json.hpp"
#include "TempDir.hpp"

namespace
{
  using Comprendo::ChartTools;
  using Comprendo::Json;
  using Comprendo::NodeKind;
  using Comprendo::Testing::TempDir;

  /// Run a tool and return its output, or the error text prefixed with "error: ".
  ///
  /// \param tools Tools of the scratch project.
  /// \param name Tool name.
  /// \param args Arguments.
  /// \return Output or error text.
  std::string
  call(const ChartTools &tools, std::string_view name, Json::Object args = {})
  {
    const auto result = tools.call(name, Json {std::move(args)});
    return result ? *result : "error: " + result.error();
  }

  /// Check whether a text contains a fragment.
  ///
  /// \param text Text.
  /// \param fragment Expected part.
  /// \return True when \p fragment occurs in \p text.
  bool
  contains(std::string_view text, std::string_view fragment)
  {
    return text.find(fragment) != std::string_view::npos;
  }

  /// Id of `geo::area` as added by add_area().
  ///
  /// \return The extractor's id for the node.
  std::uint64_t
  area_id()
  {
    return Comprendo::make_node_id(NodeKind::Function, "geo::area", "src/geo.cpp", "(double)");
  }

  /// Add `geo::area` in `src/geo.cpp`.
  ///
  /// \param tools Tools of the scratch project.
  /// \return Tool output.
  std::string
  add_area(const ChartTools &tools)
  {
    return call(
      tools,
      "chart_add_node",
      {
        {"kind", "function"},
        {"qualified_name", "geo::area"},
        {"file", "src/geo.cpp"},
        {"line_start", 3},
        {"line_end", 7},
        {"signature", "(double)"},
        {"summary", "Area of a disk"},
        {"detail", ""},
        {"parent", ""},
      }
    );
  }

  /// Read the scratch project's chart.
  ///
  /// \param tools Tools of the scratch project.
  /// \return The chart, or an empty one when it cannot be read.
  Comprendo::Chart
  load(const ChartTools &tools)
  {
    return Comprendo::read_chart(tools.chart_path()).value_or(Comprendo::Chart {});
  }

  /// Read one `meta` value with SQLite directly (the Chart struct has no annotator).
  ///
  /// \param path Chart file.
  /// \param key Meta key.
  /// \return The value, or nothing.
  std::optional<std::string>
  meta_value(const std::filesystem::path &path, const char *key)
  {
    sqlite3 *db = nullptr;
    std::optional<std::string> value;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK)
    {
      sqlite3_stmt *stmt = nullptr;
      if (
        sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key = ?", -1, &stmt, nullptr)
        == SQLITE_OK
      )
      {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
          // SQLite returns UTF-8 text as unsigned char.
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        }
      }
      sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return value;
  }
} // namespace

/// Without the extractor the model builds the chart itself, so adding a symbol
/// must also create its file and the Contains edge the viewer's tree needs,
/// with the id the extractor would compute. Adds a function to an empty
/// project, then checks both rows, their provenance, the edge, and that adding
/// it again reports the existing node instead of failing.
TEST_CASE("chart_add_node adds the node under its file with the extractor's id", "[tools]")
{
  const TempDir project {"tools-add-node"};
  const ChartTools tools {project.path()};
  CHECK(contains(add_area(tools), Comprendo::format_id(area_id())));
  const Comprendo::Chart chart = load(tools);
  CHECK(chart.nodes.size() == 2);
  const std::uint64_t file_id
    = Comprendo::make_node_id(NodeKind::File, "src/geo.cpp", "src/geo.cpp", "");
  const auto file = std::ranges::find(chart.nodes, file_id, &Comprendo::Node::id);
  const auto area = std::ranges::find(chart.nodes, area_id(), &Comprendo::Node::id);
  CHECK(file != chart.nodes.end());
  CHECK(area != chart.nodes.end());
  if (file == chart.nodes.end() || area == chart.nodes.end())
  {
    return;
  }
  CHECK(file->name == "geo.cpp");
  CHECK(area->name == "area");
  CHECK(area->kind == NodeKind::Function);
  CHECK(
    std::ranges::all_of(
      chart.nodes, [](const auto &node) { return node.provenance == Comprendo::Provenance::Model; }
    )
  );
  CHECK(chart.edges.size() == 1);
  CHECK(
    std::ranges::any_of(
      chart.edges,
      [&](const auto &edge)
      {
        return edge.src == file_id && edge.dst == area_id()
          && edge.kind == Comprendo::EdgeKind::Contains;
      }
    )
  );
  CHECK(contains(add_area(tools), "already in the chart"));
}

/// Models make mistakes in arguments; each must come back as an error the
/// model can read and fix, and nothing may be written. Calls chart_add_node
/// with an unknown kind, a path outside the project, a missing name, a parent
/// that does not exist, and a negative line, then checks the chart is empty.
TEST_CASE("chart_add_node rejects bad arguments without writing", "[tools]")
{
  const TempDir project {"tools-bad-args"};
  const ChartTools tools {project.path()};
  // Valid arguments with one of them replaced or added.
  const auto add = [&](const std::string &key, Json value)
  {
    Json::Object args {{"kind", "function"}, {"qualified_name", "f"}, {"file", "a.cpp"}};
    const auto found = std::ranges::find(args, key, &std::pair<std::string, Json>::first);
    if (found == args.end())
    {
      args.emplace_back(key, std::move(value));
    }
    else
    {
      found->second = std::move(value);
    }
    return call(tools, "chart_add_node", std::move(args));
  };
  CHECK(contains(add("kind", "lambda"), "error: unknown kind 'lambda'"));
  CHECK(contains(add("file", "../a.cpp"), "outside the project"));
  CHECK(contains(add("qualified_name", ""), "qualified_name is required"));
  CHECK(contains(add("parent", "0x1"), "error: no node with id 0x0000000000000001"));
  CHECK(contains(add("line_start", -1), "non-negative"));
  CHECK(load(tools).nodes.empty());
}

/// Annotating is the main job of /chart: the model finds a node, then writes
/// its explanation. Adds a node, finds it by short name and by directory,
/// checks a miss, annotates the detail only, and checks the summary is kept,
/// the detail written, and an empty annotation refused.
TEST_CASE("chart_find_node and chart_annotate_node", "[tools]")
{
  const TempDir project {"tools-find-annotate"};
  const ChartTools tools {project.path()};
  add_area(tools);
  CHECK(contains(call(tools, "chart_find_node", {{"query", "area"}}), "geo::area (double)"));
  CHECK(contains(
    call(tools, "chart_find_node", {{"file", "src/"}, {"kind", "File"}, {"limit", 5}}),
    "file src/geo.cpp src/geo.cpp"
  ));
  CHECK(call(tools, "chart_find_node", {{"query", "nothing"}}) == "no matching nodes");
  CHECK(
    call(
      tools,
      "chart_annotate_node",
      {{"id", Comprendo::format_id(area_id())}, {"detail", "Uses pi * r^2."}}
    )
    == "annotated geo::area"
  );
  const Comprendo::Chart chart = load(tools);
  const auto area = std::ranges::find(chart.nodes, area_id(), &Comprendo::Node::id);
  CHECK(area != chart.nodes.end());
  if (area != chart.nodes.end())
  {
    CHECK(area->summary == "Area of a disk");
    CHECK(area->detail == "Uses pi * r^2.");
  }
  CHECK(contains(
    call(tools, "chart_annotate_node", {{"id", Comprendo::format_id(area_id())}}),
    "give a summary, a detail, or both"
  ));
}

/// Summaries the extractor copied from Doxygen must be visible as such, so the
/// model keeps good ones, and progress must count them apart from the model's
/// own. Writes a chart with one documented extractor node, checks the [doc]
/// mark and the overview's counts, annotates it, and checks it moves to the
/// model's count.
TEST_CASE("code documentation is marked and counted apart", "[tools]")
{
  const TempDir project {"tools-doc"};
  const ChartTools tools {project.path()};
  const std::uint64_t file_id
    = Comprendo::make_node_id(NodeKind::File, "src/geo.cpp", "src/geo.cpp", "");
  const std::uint64_t volume_id
    = Comprendo::make_node_id(NodeKind::Function, "geo::volume", "src/geo.cpp", "()");
  Comprendo::Chart seed;
  seed.nodes = {
    {.id = file_id,
     .kind = NodeKind::File,
     .name = "geo.cpp",
     .qualified_name = "src/geo.cpp",
     .file = "src/geo.cpp",
     .provenance = Comprendo::Provenance::Extractor},
    {.id = volume_id,
     .kind = NodeKind::Function,
     .name = "volume",
     .qualified_name = "geo::volume",
     .file = "src/geo.cpp",
     .line_start = 1,
     .line_end = 2,
     .signature = "()",
     .summary = "Volume of a ball.",
     .provenance = Comprendo::Provenance::Extractor},
  };
  seed.edges = {{.src = file_id, .dst = volume_id, .kind = Comprendo::EdgeKind::Contains}};
  CHECK(Comprendo::write_chart(tools.chart_path(), seed).has_value());

  CHECK(
    contains(call(tools, "chart_find_node", {{"query", "volume"}}), "— [doc] Volume of a ball.")
  );
  CHECK(contains(call(tools, "chart_overview"), "src/geo.cpp  1 1 0"));
  call(
    tools,
    "chart_annotate_node",
    {{"id", Comprendo::format_id(volume_id)},
     {"summary", "Volume of a ball, used for collision radii."}}
  );
  CHECK(
    contains(call(tools, "chart_find_node", {{"query", "volume"}}), "— Volume of a ball, used")
  );
  CHECK(contains(call(tools, "chart_overview"), "src/geo.cpp  1 0 1"));
}

/// Clusters are addressed by label so the model can refer to them without
/// ids; nesting must stay acyclic, and one bad argument must not leave a half
/// written cluster. Creates a parent and a child with a member, then tries a
/// cycle, an unknown parent, an unknown member, and a bad colour, and checks
/// only the two good clusters exist with the expected colour and nesting.
TEST_CASE("chart_annotate_cluster nests clusters and assigns members atomically", "[tools]")
{
  const TempDir project {"tools-clusters"};
  const ChartTools tools {project.path()};
  add_area(tools);
  CHECK(contains(
    call(tools, "chart_annotate_cluster", {{"label", "Geometry"}, {"summary", "Shapes"}}),
    "created cluster Geometry"
  ));
  CHECK(contains(
    call(
      tools,
      "chart_annotate_cluster",
      {{"label", "Areas"},
       {"summary", "Area formulas"},
       {"color", "#336699"},
       {"parent_label", "Geometry"},
       {"members", Json::Array {Comprendo::format_id(area_id())}}}
    ),
    "assigned 1 nodes"
  ));
  CHECK(contains(
    call(tools, "chart_annotate_cluster", {{"label", "Geometry"}, {"parent_label", "Areas"}}),
    "cycle"
  ));
  CHECK(contains(
    call(tools, "chart_annotate_cluster", {{"label", "X"}, {"parent_label", "Nope"}}),
    "no cluster labelled 'Nope'"
  ));
  CHECK(contains(
    call(tools, "chart_annotate_cluster", {{"label", "Y"}, {"members", Json::Array {"0x1"}}}),
    "no node"
  ));
  CHECK(
    contains(call(tools, "chart_annotate_cluster", {{"label", "Z"}, {"color", "teal"}}), "#4a90d9")
  );

  const Comprendo::Chart chart = load(tools);
  CHECK(chart.clusters.size() == 2);
  const auto areas = std::ranges::find(chart.clusters, "Areas", &Comprendo::Cluster::label);
  CHECK(areas != chart.clusters.end());
  if (areas != chart.clusters.end())
  {
    CHECK(areas->color == 0x336699U);
    CHECK(areas->parent_id == Comprendo::make_cluster_id("Geometry"));
  }
  CHECK(
    std::ranges::count(
      chart.nodes, Comprendo::make_cluster_id("Areas"), &Comprendo::Node::cluster_id
    )
    == 1
  );
}

/// Edges the model writes must merge with extractor edges instead of failing
/// on the (src, dst, kind) key, and chart_finish must leave a chart the
/// viewer opens. Adds two nodes and one call edge twice (the second time
/// without evidence), checks the merged row and two refusals, then finishes
/// and checks the stamps and the printed viewer command.
TEST_CASE("chart_add_edge upserts and chart_finish stamps the chart", "[tools]")
{
  const TempDir project {"tools-edges"};
  const ChartTools tools {project.path()};
  add_area(tools);
  call(
    tools,
    "chart_add_node",
    {{"kind", "function"},
     {"qualified_name", "main"},
     {"file", "src/main.cpp"},
     {"signature", "()"}}
  );
  const std::string main_id = Comprendo::format_id(
    Comprendo::make_node_id(NodeKind::Function, "main", "src/main.cpp", "()")
  );
  const std::string area_text = Comprendo::format_id(area_id());
  const auto edge = [&](Json::Object extra)
  {
    Json::Object args {{"src", main_id}, {"dst", area_text}, {"kind", "calls"}};
    args.insert(args.end(), extra.begin(), extra.end());
    return call(tools, "chart_add_edge", std::move(args));
  };
  CHECK(edge({{"weight", 2}, {"evidence", "src/main.cpp:4"}}).starts_with("added"));
  CHECK(edge({{"weight", 3}, {"evidence", ""}}).starts_with("updated"));
  CHECK(contains(
    call(tools, "chart_add_edge", {{"src", main_id}, {"dst", main_id}, {"kind", "calls"}}),
    "two different"
  ));
  CHECK(contains(
    call(tools, "chart_add_edge", {{"src", main_id}, {"dst", area_text}, {"kind", "owns"}}),
    "unknown edge kind"
  ));

  const std::string finished = call(tools, "chart_finish");
  CHECK(contains(finished, "comprendo " + tools.chart_path().string()));
  CHECK(contains(finished, "1 of 2 symbols annotated by the model, 0 documented in code"));
  const Comprendo::Chart chart = load(tools);
  const auto calls
    = std::ranges::find(chart.edges, Comprendo::EdgeKind::Calls, &Comprendo::Edge::kind);
  CHECK(calls != chart.edges.end());
  if (calls != chart.edges.end())
  {
    CHECK(calls->weight == 3.0F);
    CHECK(calls->evidence == "src/main.cpp:4");
  }
  CHECK(chart.generator.starts_with("comprendo-cli"));
  CHECK(meta_value(tools.chart_path(), "annotator").value_or("").starts_with("comprendo-cli"));
  CHECK(Comprendo::check_chart_file(tools.chart_path()).ok());
}

/// The model copies ids between tools, so formatting and parsing must round
/// trip and reject malformed text, and anything that is not a chart must be
/// left alone. Checks hex and decimal ids, too-long and junk ids, the cluster
/// id rule, an unknown tool, and a foreign SQLite file in the chart's place.
TEST_CASE("ids, unknown tools, and foreign files", "[tools]")
{
  CHECK(Comprendo::format_id(0xABCULL) == "0x0000000000000abc");
  CHECK(Comprendo::parse_id(" 0x0000000000000ABC ") == 0xABCULL);
  CHECK(Comprendo::parse_id("18446744073709551615") == 0xFFFFFFFFFFFFFFFFULL);
  CHECK_FALSE(Comprendo::parse_id("18446744073709551616"));
  CHECK_FALSE(Comprendo::parse_id("0x10000000000000000"));
  CHECK_FALSE(Comprendo::parse_id("0xfoo"));
  CHECK_FALSE(Comprendo::parse_id(""));
  CHECK(Comprendo::make_cluster_id("Geometry") == Comprendo::fnv1a64("cluster\x1fGeometry"));
  CHECK(Comprendo::make_cluster_id("Geometry") != 0);

  const TempDir project {"tools-foreign"};
  const ChartTools tools {project.path()};
  CHECK(call(tools, "chart_nope") == "error: unknown tool chart_nope");
  std::filesystem::create_directories(tools.chart_path().parent_path());
  sqlite3 *db = nullptr;
  CHECK(sqlite3_open(tools.chart_path().c_str(), &db) == SQLITE_OK);
  CHECK(sqlite3_exec(db, "CREATE TABLE other (x)", nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(db);
  CHECK(contains(call(tools, "chart_overview"), "is not a Comprendo chart"));
}
