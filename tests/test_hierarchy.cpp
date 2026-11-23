/// \file
/// \brief Tests for grouping charts by file, namespace, and cluster.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
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

  /// \name Fixed node ids of the test chart, chosen so that sorting by id is meaningful.
  ///@{
  constexpr std::uint64_t kFileA = 10;
  constexpr std::uint64_t kFileB = 11;
  constexpr std::uint64_t kGeoA = 20;
  constexpr std::uint64_t kGeoB = 21;
  constexpr std::uint64_t kDetailA = 22;
  constexpr std::uint64_t kAnonB = 23;
  constexpr std::uint64_t kShape = 30;
  constexpr std::uint64_t kFunctionF = 31;
  constexpr std::uint64_t kHelper = 32;
  constexpr std::uint64_t kMain = 33;
  constexpr std::uint64_t kTwoParents = 40;
  constexpr std::uint64_t kCycleX = 50;
  constexpr std::uint64_t kCycleY = 51;
  ///@}

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

namespace
{
  /// Label of the parent group of a node's group.
  ///
  /// \param chart Grouped chart.
  /// \param hierarchy Hierarchy of \p chart.
  /// \param id Node id.
  /// \return The parent group's label.
  std::string
  parent_label(const Chart &chart, const Hierarchy &hierarchy, std::uint64_t id)
  {
    const auto node = std::ranges::find(chart.nodes, id, &Comprendo::Node::id);
    const std::size_t group
      = hierarchy.group_of_node.at(static_cast<std::size_t>(node - chart.nodes.begin()));
    return hierarchy.groups.at(hierarchy.groups.at(group).parent).label;
  }

  /// Check that every group reaches the root through its parents.
  ///
  /// \param hierarchy Hierarchy to check.
  /// \return True when no parent chain loops.
  bool
  is_tree(const Hierarchy &hierarchy)
  {
    for (std::size_t group = 0; group < hierarchy.groups.size(); ++group)
    {
      std::size_t current = group;
      for (std::size_t steps = 0; current != 0; ++steps)
      {
        if (steps > hierarchy.groups.size())
        {
          return false;
        }
        current = hierarchy.groups.at(current).parent;
      }
    }
    return true;
  }
} // namespace

/// The cluster view shows the subsystems the model named, so nodes must move
/// into their cluster with their contents while members of other clusters move
/// out. Assigns file a.cpp to "Geometry" and class Shape (inside a.cpp) to the
/// nested "Shapes", then checks where a.cpp, its namespace, Shape, the nested
/// cluster, and the unclustered b.cpp end up.
TEST_CASE("cluster grouping nests nodes under their clusters", "[hierarchy]")
{
  Chart chart = sample_chart();
  chart.clusters.push_back({.id = 900, .label = "Geometry"});
  chart.clusters.push_back({.id = 901, .label = "Shapes", .parent_id = 900});
  chart.nodes.at(0).cluster_id = 900;
  chart.nodes.at(6).cluster_id = 901;
  chart.nodes.at(9).cluster_id = 12345;
  const Hierarchy hierarchy = Comprendo::build_hierarchy(chart, Grouping::Cluster);

  CHECK(parent_label(chart, hierarchy, kFileA) == "Geometry");
  CHECK(parent_label(chart, hierarchy, kGeoA) == "a.cpp");
  CHECK(parent_label(chart, hierarchy, kShape) == "Shapes");
  CHECK(parent_label(chart, hierarchy, kMain) == "a.cpp");
  CHECK(parent_label(chart, hierarchy, kFileB) == "(unclustered)");
  const auto shapes
    = std::ranges::find(hierarchy.groups, std::uint64_t {901}, &Comprendo::HierarchyGroup::cluster);
  CHECK(shapes != hierarchy.groups.end());
  if (shapes != hierarchy.groups.end())
  {
    CHECK(hierarchy.groups.at(shapes->parent).label == "Geometry");
    CHECK(shapes->nodes.empty());
  }
  CHECK(is_tree(hierarchy));
}

/// Cluster parents come from the model and may loop or be missing, and charts
/// without clusters must still show something sensible in the cluster view.
/// Builds clusters that are each other's parent and checks the result is a tree;
/// then checks a chart without clusters groups exactly like the file grouping.
TEST_CASE("cluster grouping tolerates cycles and missing clusters", "[hierarchy]")
{
  Chart looping = sample_chart();
  looping.clusters.push_back({.id = 902, .label = "P", .parent_id = 903});
  looping.clusters.push_back({.id = 903, .label = "Q", .parent_id = 902});
  looping.nodes.at(1).cluster_id = 902;
  CHECK(is_tree(Comprendo::build_hierarchy(looping, Grouping::Cluster)));

  const Chart plain = sample_chart();
  const Hierarchy by_cluster = Comprendo::build_hierarchy(plain, Grouping::Cluster);
  const Hierarchy by_file = Comprendo::build_hierarchy(plain, Grouping::File);
  CHECK(by_cluster.groups.size() == by_file.groups.size());
  CHECK(describe(plain, by_cluster) == describe(plain, by_file));
  CHECK(Comprendo::next_grouping(Grouping::File) == Grouping::Namespace);
  CHECK(Comprendo::next_grouping(Grouping::Namespace) == Grouping::Cluster);
  CHECK(Comprendo::next_grouping(Grouping::Cluster) == Grouping::File);
  CHECK(Comprendo::to_string(Grouping::Cluster) == "cluster");
}
