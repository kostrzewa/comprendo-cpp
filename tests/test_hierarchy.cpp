/// \file
/// \brief Tests for grouping charts by file and by namespace.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "Comprendo/Chart.hpp"
#include "Comprendo/Hierarchy.hpp"

namespace
{
  using Comprendo::Chart;
  using Comprendo::EdgeKind;
  using Comprendo::Grouping;
  using Comprendo::Hierarchy;
  using Comprendo::NodeKind;

  /// Fixed node ids of the test chart, chosen so that sorting by id is meaningful.
  enum Id : std::uint64_t
  {
    kFileA = 10,
    kFileB = 11,
    kGeoA = 20,
    kGeoB = 21,
    kDetailA = 22,
    kAnonB = 23,
    kShape = 30,
    kFunctionF = 31,
    kHelper = 32,
    kMain = 33,
    kTwoParents = 40,
    kCycleX = 50,
    kCycleY = 51,
  };

  /// Build a chart covering merged, nested, and anonymous namespaces, a node with
  /// two `Contains` parents, and a `Contains` cycle.
  ///
  /// \return The chart.
  Chart
  sample_chart()
  {
    Chart chart;
    const auto node
      = [&chart](std::uint64_t id, NodeKind kind, const char *name, const char *qualified)
    { chart.nodes.push_back({.id = id, .kind = kind, .name = name, .qualified_name = qualified}); };
    node(kFileA, NodeKind::File, "a.cpp", "a.cpp");
    node(kFileB, NodeKind::File, "b.cpp", "b.cpp");
    node(kGeoA, NodeKind::Namespace, "geo", "geo");
    node(kGeoB, NodeKind::Namespace, "geo", "geo");
    node(kDetailA, NodeKind::Namespace, "detail", "geo::detail");
    node(kAnonB, NodeKind::Namespace, "__anon1f2e", "__anon1f2e");
    node(kShape, NodeKind::Class, "Shape", "geo::Shape");
    node(kFunctionF, NodeKind::Function, "f", "geo::f");
    node(kHelper, NodeKind::Function, "helper", "__anon1f2e::helper");
    node(kMain, NodeKind::Function, "main", "main");
    node(kTwoParents, NodeKind::Function, "g", "geo::g");
    node(kCycleX, NodeKind::Other, "x", "x");
    node(kCycleY, NodeKind::Other, "y", "y");
    const auto contains = [&chart](std::uint64_t src, std::uint64_t dst)
    { chart.edges.push_back({.src = src, .dst = dst, .kind = EdgeKind::Contains}); };
    contains(kFileA, kGeoA);
    contains(kFileB, kGeoB);
    contains(kGeoA, kDetailA);
    contains(kFileB, kAnonB);
    contains(kGeoA, kShape);
    contains(kGeoB, kFunctionF);
    contains(kAnonB, kHelper);
    contains(kFileA, kMain);
    contains(kGeoB, kTwoParents);
    contains(kGeoA, kTwoParents);
    contains(kCycleX, kCycleY);
    contains(kCycleY, kCycleX);
    chart.edges.push_back({.src = kMain, .dst = kFunctionF, .kind = EdgeKind::Calls});
    return chart;
  }

  /// Describe a hierarchy by node ids so it can be compared across node orders.
  ///
  /// \param chart Chart the hierarchy was built from.
  /// \param hierarchy Hierarchy to describe.
  /// \return For each node id, the sorted ids of the nodes in its parent group
  ///         (empty for children of the root).
  std::map<std::uint64_t, std::vector<std::uint64_t>>
  describe(const Chart &chart, const Hierarchy &hierarchy)
  {
    std::map<std::uint64_t, std::vector<std::uint64_t>> parents;
    for (std::size_t node = 0; node < chart.nodes.size(); ++node)
    {
      const std::size_t group = hierarchy.group_of_node.at(node);
      std::vector<std::uint64_t> ids;
      for (const std::size_t member : hierarchy.groups.at(hierarchy.groups.at(group).parent).nodes)
      {
        ids.push_back(chart.nodes.at(member).id);
      }
      std::ranges::sort(ids);
      parents[chart.nodes.at(node).id] = ids;
    }
    return parents;
  }
} // namespace

/// The file grouping is the stored containment tree, which the Explorer and the
/// layout use directly, so it must follow `Contains` edges faithfully. Builds the
/// sample chart grouped by file and checks the parent of representative nodes,
/// that the two `geo` namespaces stay separate, that the smallest parent id wins
/// for a node with two parents, and that a containment cycle is cut.
TEST_CASE("file grouping follows containment", "[hierarchy]")
{
  const Chart chart = sample_chart();
  const Hierarchy hierarchy = Comprendo::build_hierarchy(chart, Grouping::File);
  const auto parents = describe(chart, hierarchy);

  CHECK(hierarchy.groups.size() == chart.nodes.size() + 1);
  CHECK(parents.at(kFileA).empty());
  CHECK(parents.at(kGeoA) == std::vector<std::uint64_t> {kFileA});
  CHECK(parents.at(kDetailA) == std::vector<std::uint64_t> {kGeoA});
  CHECK(parents.at(kShape) == std::vector<std::uint64_t> {kGeoA});
  CHECK(parents.at(kAnonB) == std::vector<std::uint64_t> {kFileB});
  CHECK(parents.at(kMain) == std::vector<std::uint64_t> {kFileA});
  CHECK(parents.at(kTwoParents) == std::vector<std::uint64_t> {kGeoA});
  CHECK(hierarchy.group_of_node.at(2) != hierarchy.group_of_node.at(3));
  const bool x_root = parents.at(kCycleX).empty();
  const bool y_root = parents.at(kCycleY).empty();
  CHECK(x_root != y_root);
}

/// The namespace view merges namespaces declared in several files, which the
/// viewer offers as a second grouping. Builds the sample chart grouped by
/// namespace and checks the merged `geo` group (label, both member nodes),
/// nesting of `geo::detail`, members from both files under the merged group,
/// anonymous namespaces kept separate, and file nodes as childless root leaves.
TEST_CASE("namespace grouping merges namespaces", "[hierarchy]")
{
  const Chart chart = sample_chart();
  const Hierarchy hierarchy = Comprendo::build_hierarchy(chart, Grouping::Namespace);
  const auto parents = describe(chart, hierarchy);

  const std::size_t geo = hierarchy.group_of_node.at(2);
  CHECK(hierarchy.group_of_node.at(3) == geo);
  CHECK(hierarchy.groups.at(geo).label == "geo");
  CHECK(hierarchy.groups.at(geo).nodes == std::vector<std::size_t> {2, 3});
  CHECK(hierarchy.groups.at(geo).parent == 0);

  const std::vector<std::uint64_t> merged_geo {kGeoA, kGeoB};
  CHECK(parents.at(kDetailA) == merged_geo);
  CHECK(parents.at(kShape) == merged_geo);
  CHECK(parents.at(kFunctionF) == merged_geo);
  CHECK(parents.at(kTwoParents) == merged_geo);
  CHECK(parents.at(kAnonB).empty());
  CHECK(parents.at(kHelper) == std::vector<std::uint64_t> {kAnonB});
  CHECK(parents.at(kMain).empty());
  CHECK(parents.at(kFileA).empty());
  CHECK(hierarchy.groups.at(hierarchy.group_of_node.at(0)).children.empty());
}

/// Layout and Explorer must not change when the same chart is stored in another
/// order, since SQLite row order is arbitrary. Reverses the nodes and edges of
/// the sample chart and checks that both groupings describe the same parents for
/// every node id and that children are listed in the same key order.
TEST_CASE("hierarchy does not depend on chart order", "[hierarchy]")
{
  const Chart chart = sample_chart();
  Chart reversed = chart;
  std::ranges::reverse(reversed.nodes);
  std::ranges::reverse(reversed.edges);

  for (const Grouping grouping : {Grouping::File, Grouping::Namespace})
  {
    const Hierarchy forward = Comprendo::build_hierarchy(chart, grouping);
    const Hierarchy backward = Comprendo::build_hierarchy(reversed, grouping);
    CHECK(describe(chart, forward) == describe(reversed, backward));
    std::vector<std::uint64_t> forward_keys;
    std::vector<std::uint64_t> backward_keys;
    for (const std::size_t child : forward.groups.front().children)
    {
      forward_keys.push_back(forward.groups.at(child).key);
    }
    for (const std::size_t child : backward.groups.front().children)
    {
      backward_keys.push_back(backward.groups.at(child).key);
    }
    CHECK(forward_keys == backward_keys);
    CHECK(std::ranges::is_sorted(forward_keys));
  }
}
