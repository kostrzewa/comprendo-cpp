/// \file
/// \brief Tests for chart ids, canonical ordering, and validation.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "Comprendo/Chart.hpp"

namespace
{
  using Comprendo::Chart;
  using Comprendo::Edge;
  using Comprendo::EdgeKind;
  using Comprendo::NodeKind;
  using Comprendo::Provenance;

  /// Build a minimal valid chart: two nodes in one cluster joined by a call edge.
  ///
  /// \return Valid chart.
  Chart
  valid_chart()
  {
    Chart chart;
    chart.clusters.push_back({.id = 7, .label = "core"});
    chart.nodes.push_back({.id = 1, .kind = NodeKind::Function, .name = "a", .cluster_id = 7});
    chart.nodes.push_back({.id = 2, .kind = NodeKind::Function, .name = "b"});
    chart.edges.push_back({.src = 1, .dst = 2, .kind = EdgeKind::Calls});
    return chart;
  }

  /// Check that a chart fails validation with an error mentioning a phrase.
  ///
  /// \param chart Chart expected to be invalid.
  /// \param phrase Text one of the errors must contain.
  void
  require_error(const Chart &chart, const std::string &phrase)
  {
    const Comprendo::ValidationResult result = Comprendo::validate(chart);
    REQUIRE_FALSE(result.ok());
    bool found = false;
    for (const std::string &error : result.errors)
    {
      found = found || error.find(phrase) != std::string::npos;
    }
    INFO("expected an error containing: " << phrase);
    REQUIRE(found);
  }
} // namespace

/// Kind names match plan/chart-schema.md and flag out-of-range values.
TEST_CASE("kind names", "[chart]")
{
  CHECK(Comprendo::to_string(NodeKind::Other) == "Other");
  CHECK(Comprendo::to_string(NodeKind::Method) == "Method");
  CHECK(Comprendo::to_string(NodeKind::TypeAlias) == "TypeAlias");
  CHECK(Comprendo::to_string(static_cast<NodeKind>(14)) == "Invalid");
  CHECK(Comprendo::to_string(EdgeKind::Contains) == "Contains");
  CHECK(Comprendo::to_string(EdgeKind::DataFlow) == "DataFlow");
  CHECK(Comprendo::to_string(static_cast<EdgeKind>(7)) == "Invalid");
}

/// FNV-1a matches the published 64-bit test vectors.
TEST_CASE("fnv1a64 matches reference vectors", "[chart][id]")
{
  CHECK(Comprendo::fnv1a64("") == 0xcbf29ce484222325ULL);
  CHECK(Comprendo::fnv1a64("a") == 0xaf63dc4c8601ec8cULL);
  CHECK(Comprendo::fnv1a64("foobar") == 0x85944171f73967e8ULL);
}

/// Node ids match the vectors documented in plan/chart-schema.md for the plugin.
TEST_CASE("make_node_id matches documented vectors", "[chart][id]")
{
  using Comprendo::make_node_id;
  CHECK(
    make_node_id(NodeKind::Function, "main", "src/main.cpp", "int main(int, char **)")
    == 0x45010832c7701fd0ULL
  );
  CHECK(
    make_node_id(
      NodeKind::Method,
      "Comprendo::Chart::size",
      "include/Comprendo/Chart.hpp",
      "std::size_t () const"
    )
    == 0x2628ff0989741339ULL
  );
  CHECK(make_node_id(NodeKind::File, "src/main.cpp", "src/main.cpp", "") == 0xaa52bbd69c1499c1ULL);
  CHECK(make_node_id(NodeKind::Function, "f", "a.cpp", "void (int)") == 0x7de5b0f108cc13c0ULL);
  CHECK(make_node_id(NodeKind::Function, "f", "a.cpp", "void (double)") == 0x2f0a08d143e85434ULL);
}

/// Every identity field contributes to the id, and field boundaries matter.
TEST_CASE("make_node_id distinguishes every identity field", "[chart][id]")
{
  using Comprendo::make_node_id;
  const std::uint64_t base = make_node_id(NodeKind::Function, "f", "a.cpp", "void (int)");
  CHECK(base == make_node_id(NodeKind::Function, "f", "a.cpp", "void (int)"));
  CHECK(base != make_node_id(NodeKind::Method, "f", "a.cpp", "void (int)"));
  CHECK(base != make_node_id(NodeKind::Function, "g", "a.cpp", "void (int)"));
  CHECK(base != make_node_id(NodeKind::Function, "f", "b.cpp", "void (int)"));
  CHECK(base != make_node_id(NodeKind::Function, "f", "a.cpp", "void (double)"));
  CHECK(
    make_node_id(NodeKind::Function, "ab", "c", "")
    != make_node_id(NodeKind::Function, "a", "bc", "")
  );
}

/// Canonical order sorts by id and keeps positions attached to their nodes.
TEST_CASE("canonicalize sorts and keeps layout aligned", "[chart]")
{
  Chart chart;
  chart.nodes = {{.id = 30}, {.id = 10}, {.id = 20}};
  chart.layout = {{.x = 3.0F}, {.x = 1.0F}, {.x = 2.0F}};
  chart.clusters = {{.id = 9}, {.id = 4}};
  chart.edges = {
    {.src = 20, .dst = 10, .kind = EdgeKind::Calls},
    {.src = 10, .dst = 30, .kind = EdgeKind::Uses},
    {.src = 10, .dst = 30, .kind = EdgeKind::Calls},
  };

  Comprendo::canonicalize(chart);

  CHECK(chart.nodes[0].id == 10);
  CHECK(chart.nodes[1].id == 20);
  CHECK(chart.nodes[2].id == 30);
  CHECK(chart.layout[0].x == 1.0F);
  CHECK(chart.layout[1].x == 2.0F);
  CHECK(chart.layout[2].x == 3.0F);
  CHECK(chart.clusters[0].id == 4);
  CHECK(chart.edges[0].kind == EdgeKind::Calls);
  CHECK(chart.edges[0].dst == 30);
  CHECK(chart.edges[1].kind == EdgeKind::Uses);
  CHECK(chart.edges[2].src == 20);
}

/// A well-formed chart passes validation without errors.
TEST_CASE("valid chart passes validation", "[chart][validate]")
{
  const Comprendo::ValidationResult result = Comprendo::validate(valid_chart());
  CHECK(result.ok());
  CHECK(result.errors.empty());
}

/// Each validation rule from plan/chart-schema.md rejects its violation.
TEST_CASE("validation rejects every rule violation", "[chart][validate]")
{
  Chart chart = valid_chart();

  SECTION("unsupported schema version")
  {
    chart.schema_version = Comprendo::kChartSchemaVersion + 1;
    require_error(chart, "unsupported schema version");
  }
  SECTION("duplicate node id")
  {
    chart.nodes.push_back(chart.nodes.front());
    require_error(chart, "duplicate node id 1");
  }
  SECTION("node kind out of range")
  {
    chart.nodes[0].kind = static_cast<NodeKind>(14);
    require_error(chart, "node 1 has an out-of-range kind");
  }
  SECTION("node provenance out of range")
  {
    chart.nodes[0].provenance = static_cast<Provenance>(3);
    require_error(chart, "node 1 has an out-of-range provenance");
  }
  SECTION("zero cluster id")
  {
    chart.clusters.push_back({.id = 0, .label = "zero"});
    require_error(chart, "cluster id must be non-zero");
  }
  SECTION("duplicate cluster id")
  {
    chart.clusters.push_back({.id = 7, .label = "again"});
    require_error(chart, "duplicate cluster id 7");
  }
  SECTION("missing parent cluster")
  {
    chart.clusters[0].parent_id = 99;
    require_error(chart, "cluster 7 references missing parent 99");
  }
  SECTION("cluster parent cycle")
  {
    chart.clusters[0].parent_id = 8;
    chart.clusters.push_back({.id = 8, .label = "loop", .parent_id = 7});
    require_error(chart, "cluster 7 is its own ancestor");
    require_error(chart, "cluster 8 is its own ancestor");
  }
  SECTION("cluster as its own parent")
  {
    chart.clusters[0].parent_id = 7;
    require_error(chart, "cluster 7 is its own ancestor");
  }
  SECTION("node in missing cluster")
  {
    chart.nodes[1].cluster_id = 42;
    require_error(chart, "node 2 references missing cluster 42");
  }
  SECTION("edge kind out of range")
  {
    chart.edges[0].kind = static_cast<EdgeKind>(7);
    require_error(chart, "edge 1 -> 2 has an out-of-range kind");
  }
  SECTION("edge provenance out of range")
  {
    chart.edges[0].provenance = static_cast<Provenance>(3);
    require_error(chart, "edge 1 -> 2 has an out-of-range provenance");
  }
  SECTION("missing edge source")
  {
    chart.edges[0].src = 5;
    require_error(chart, "edge source 5 does not exist");
  }
  SECTION("missing edge destination")
  {
    chart.edges[0].dst = 6;
    require_error(chart, "edge destination 6 does not exist");
  }
  SECTION("duplicate edge key")
  {
    Edge duplicate = chart.edges[0];
    duplicate.weight = 2.0F;
    chart.edges.push_back(duplicate);
    require_error(chart, "duplicate edge 1 -> 2");
  }
  SECTION("layout length mismatch")
  {
    chart.layout.push_back({});
    require_error(chart, "layout has 1 positions for 2 nodes");
  }
}

/// Parallel edges of different kinds are allowed.
TEST_CASE("edges of different kinds between the same nodes are valid", "[chart][validate]")
{
  Chart chart = valid_chart();
  chart.edges.push_back({.src = 1, .dst = 2, .kind = EdgeKind::Uses});
  CHECK(Comprendo::validate(chart).ok());
}
