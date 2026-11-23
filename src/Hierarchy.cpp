/// \file
/// \brief Grouping a chart into a tree of nested groups, by file or by namespace.

#include "Comprendo/Hierarchy.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Profiler/Profiler.hpp"

namespace
{
  using Comprendo::Chart;
  using Comprendo::Hierarchy;
  using Comprendo::HierarchyGroup;

  /// Marker for "no parent" in index vectors.
  constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  /// Prefix ctags uses for anonymous entities.
  constexpr std::string_view kAnonymousPrefix = "__anon";

  /// Visit state used while cutting cycles out of a parent forest.
  enum class Visit : std::uint8_t
  {
    /// Not reached yet.
    New,
    /// On the chain currently being followed.
    Active,
    /// Known to reach a root.
    Done,
  };

  /// Cut every cycle in a parent forest so that each chain ends at a root.
  ///
  /// Chains are followed from each element in \p order; when a chain reaches an
  /// element already on it, the link into that element is removed.
  ///
  /// \param parent Parent index of each element, kNone for roots; updated in place.
  /// \param order Elements in the order to start chains from (deterministic).
  void
  cut_cycles(std::vector<std::size_t> &parent, const std::vector<std::size_t> &order)
  {
    std::vector<Visit> state(parent.size(), Visit::New);
    std::vector<std::size_t> chain;
    for (const std::size_t start : order)
    {
      chain.clear();
      std::size_t current = start;
      while (current != kNone && state[current] == Visit::New)
      {
        state[current] = Visit::Active;
        chain.push_back(current);
        const std::size_t next = parent[current];
        if (next != kNone && state[next] == Visit::Active)
        {
          parent[current] = kNone;
          break;
        }
        current = next;
      }
      for (const std::size_t element : chain)
      {
        state[element] = Visit::Done;
      }
    }
  }

  /// Find each node's `Contains` parent; the smallest parent id wins.
  ///
  /// \param chart Chart to inspect.
  /// \param index_of_id Node index by id.
  /// \return Parent node index for each node, or kNone.
  std::vector<std::size_t>
  contains_parents(
    const Chart &chart, const std::unordered_map<std::uint64_t, std::size_t> &index_of_id
  )
  {
    std::vector<std::size_t> parent(chart.nodes.size(), kNone);
    for (const Comprendo::Edge &edge : chart.edges)
    {
      if (edge.kind != Comprendo::EdgeKind::Contains || edge.src == edge.dst)
      {
        continue;
      }
      const auto src = index_of_id.find(edge.src);
      const auto dst = index_of_id.find(edge.dst);
      if (src == index_of_id.end() || dst == index_of_id.end())
      {
        continue;
      }
      std::size_t &current = parent[dst->second];
      if (current == kNone || edge.src < chart.nodes[current].id)
      {
        current = src->second;
      }
    }
    return parent;
  }

  /// Check whether a node is merged by qualified name under the given grouping.
  ///
  /// \param node Chart node.
  /// \param grouping Grouping in effect.
  /// \return True for named namespaces in Grouping::Namespace.
  /// Cluster groups added for Grouping::Cluster.
  struct ClusterGroups
  {
    /// Group of each existing cluster id.
    std::unordered_map<std::uint64_t, std::size_t> group_of_cluster;
    /// Parent group of each added group (cluster groups and the unclustered group).
    std::unordered_map<std::size_t, std::size_t> parent_of_group;
    /// Effective cluster of each node: its own, else its parent's; 0 for none.
    std::vector<std::uint64_t> effective;
    /// Group collecting top-level nodes outside every cluster; 0 (the root) when
    /// every top-level node has a cluster.
    std::size_t unclustered = 0;
  };

  /// Add one group per cluster, plus the unclustered group, and resolve each
  /// node's effective cluster.
  ///
  /// \param chart Chart being grouped.
  /// \param node_parent `Contains` parent of each node, cycles cut.
  /// \param by_id Node indices in id order.
  /// \param hierarchy Hierarchy receiving the groups.
  /// \return The added groups; empty when the chart has no clusters.
  ClusterGroups
  add_cluster_groups(
    const Chart &chart,
    const std::vector<std::size_t> &node_parent,
    const std::vector<std::size_t> &by_id,
    Hierarchy &hierarchy
  )
  {
    ClusterGroups result;
    if (chart.clusters.empty())
    {
      return result;
    }
    std::vector<const Comprendo::Cluster *> clusters;
    for (const Comprendo::Cluster &cluster : chart.clusters)
    {
      clusters.push_back(&cluster);
    }
    std::ranges::sort(clusters, {}, &Comprendo::Cluster::id);
    for (const Comprendo::Cluster *cluster : clusters)
    {
      if (result.group_of_cluster.try_emplace(cluster->id, hierarchy.groups.size()).second)
      {
        hierarchy.groups.push_back(
          {.key = cluster->id, .label = cluster->label, .cluster = cluster->id}
        );
      }
    }
    for (const Comprendo::Cluster *cluster : clusters)
    {
      const auto parent = result.group_of_cluster.find(cluster->parent_id);
      result.parent_of_group[result.group_of_cluster.at(cluster->id)]
        = parent == result.group_of_cluster.end() ? 0 : parent->second;
    }
    constexpr std::uint64_t kUnresolved = static_cast<std::uint64_t>(-1);
    result.effective.assign(chart.nodes.size(), kUnresolved);
    std::vector<std::size_t> chain;
    for (const std::size_t start : by_id)
    {
      chain.clear();
      std::size_t current = start;
      while (current != kNone && result.effective[current] == kUnresolved)
      {
        const std::uint64_t own = chart.nodes[current].cluster_id;
        if (own != 0 && result.group_of_cluster.contains(own))
        {
          result.effective[current] = own;
          break;
        }
        chain.push_back(current);
        current = node_parent[current];
      }
      const std::uint64_t inherited = current == kNone ? 0 : result.effective[current];
      for (const std::size_t element : chain)
      {
        result.effective[element] = inherited;
      }
    }
    const bool any_unclustered = std::ranges::any_of(
      by_id,
      [&](std::size_t index) { return node_parent[index] == kNone && result.effective[index] == 0; }
    );
    if (any_unclustered)
    {
      result.unclustered = hierarchy.groups.size();
      hierarchy.groups.push_back(
        {.key = Comprendo::fnv1a64("unclustered"), .label = "(unclustered)"}
      );
      result.parent_of_group[result.unclustered] = 0;
    }
    return result;
  }

  bool
  merges(const Comprendo::Node &node, Comprendo::Grouping grouping)
  {
    return grouping == Comprendo::Grouping::Namespace && node.kind == Comprendo::NodeKind::Namespace
      && !Comprendo::is_anonymous_name(node.name);
  }
} // namespace

namespace Comprendo
{
  std::string_view
  to_string(Grouping grouping)
  {
    switch (grouping)
    {
    case Grouping::File: return "file";
    case Grouping::Namespace: return "namespace";
    case Grouping::Cluster: return "cluster";
    }
    return "file";
  }

  Grouping
  next_grouping(Grouping grouping)
  {
    switch (grouping)
    {
    case Grouping::File: return Grouping::Namespace;
    case Grouping::Namespace: return Grouping::Cluster;
    case Grouping::Cluster: return Grouping::File;
    }
    return Grouping::File;
  }

  bool
  is_anonymous_name(std::string_view name)
  {
    return name.starts_with(kAnonymousPrefix);
  }

  Hierarchy
  build_hierarchy(const Chart &chart, Grouping grouping)
  {
    const ProfileScope profile {"build_hierarchy"};
    const std::size_t count = chart.nodes.size();

    std::vector<std::size_t> by_id(count);
    std::ranges::iota(by_id, std::size_t {0});
    std::ranges::sort(by_id, {}, [&chart](std::size_t index) { return chart.nodes[index].id; });

    std::unordered_map<std::uint64_t, std::size_t> index_of_id;
    index_of_id.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
      index_of_id.emplace(chart.nodes[i].id, i);
    }
    std::vector<std::size_t> node_parent = contains_parents(chart, index_of_id);
    cut_cycles(node_parent, by_id);

    Hierarchy hierarchy;
    hierarchy.groups.push_back({});
    hierarchy.group_of_node.assign(count, 0);
    std::unordered_map<std::string, std::size_t> merged;
    for (const std::size_t index : by_id)
    {
      const Node &node = chart.nodes[index];
      if (merges(node, grouping))
      {
        const auto [entry, created]
          = merged.try_emplace(node.qualified_name, hierarchy.groups.size());
        if (created)
        {
          hierarchy.groups.push_back({
            .key = fnv1a64("namespace\x1f" + node.qualified_name),
            .label = node.qualified_name,
          });
        }
        hierarchy.groups[entry->second].nodes.push_back(index);
        hierarchy.group_of_node[index] = entry->second;
        continue;
      }
      hierarchy.group_of_node[index] = hierarchy.groups.size();
      hierarchy.groups.push_back({.key = node.id, .label = node.name, .nodes = {index}});
    }

    const std::size_t node_groups = hierarchy.groups.size();
    const ClusterGroups clusters = grouping == Grouping::Cluster
      ? add_cluster_groups(chart, node_parent, by_id, hierarchy)
      : ClusterGroups {};

    const auto parent_group_of_node = [&](std::size_t index) -> std::size_t
    {
      const std::size_t parent = node_parent[index];
      if (grouping == Grouping::Cluster && !clusters.group_of_cluster.empty())
      {
        const std::uint64_t cluster = clusters.effective[index];
        if (parent != kNone && clusters.effective[parent] == cluster)
        {
          return hierarchy.group_of_node[parent];
        }
        return cluster == 0 ? clusters.unclustered : clusters.group_of_cluster.at(cluster);
      }
      if (
        parent == kNone
        || (grouping == Grouping::Namespace && chart.nodes[parent].kind == NodeKind::File)
      )
      {
        return 0;
      }
      return hierarchy.group_of_node[parent];
    };

    std::vector<std::size_t> group_parent(hierarchy.groups.size(), kNone);
    std::vector<std::size_t> group_order;
    group_order.reserve(hierarchy.groups.size());
    for (std::size_t group = 1; group < hierarchy.groups.size(); ++group)
    {
      const std::size_t parent = group < node_groups
        ? parent_group_of_node(hierarchy.groups[group].nodes.front())
        : clusters.parent_of_group.at(group);
      group_parent[group] = parent == group || parent == 0 ? kNone : parent;
      group_order.push_back(group);
    }
    cut_cycles(group_parent, group_order);

    for (const std::size_t group : group_order)
    {
      const std::size_t parent = group_parent[group] == kNone ? 0 : group_parent[group];
      hierarchy.groups[group].parent = parent;
      hierarchy.groups[parent].children.push_back(group);
    }
    for (HierarchyGroup &group : hierarchy.groups)
    {
      std::ranges::sort(
        group.children, {}, [&hierarchy](std::size_t child) { return hierarchy.groups[child].key; }
      );
    }
    return hierarchy;
  }
} // namespace Comprendo
