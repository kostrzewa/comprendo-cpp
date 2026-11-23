/// \file
/// \brief Grouping a chart into a tree of nested groups, by file or by namespace.

#include "Comprendo/Hierarchy.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <ranges>
#include <utility>
#include <tuple>
#include <set>
#include <map>
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
    clusters.reserve(chart.clusters.size());
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

  /// Directory groups added for Grouping::File.
  struct DirectoryGroups
  {
    /// Group of each directory that holds files ("" maps to the root).
    std::unordered_map<std::string, std::size_t> group_of_directory;
    /// Parent group of each added directory group.
    std::unordered_map<std::size_t, std::size_t> parent_of_group;
  };

  /// Directory part of a repository-relative path.
  ///
  /// \param path Path with '/' separators.
  /// \return Everything before the last '/', or empty.
  std::string
  parent_directory(std::string_view path)
  {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string {} : std::string {path.substr(0, slash)};
  }

  /// Add one group per directory of the top-level file nodes.
  ///
  /// Directories without files whose only child is one subdirectory are merged
  /// into that subdirectory's group, whose label then shows both ("a/b").
  ///
  /// \param chart Chart being grouped.
  /// \param node_parent `Contains` parent of each node, cycles cut.
  /// \param hierarchy Hierarchy receiving the groups.
  /// \return The added groups.
  DirectoryGroups
  add_directory_groups(
    const Chart &chart, const std::vector<std::size_t> &node_parent, Hierarchy &hierarchy
  )
  {
    struct Directory
    {
      std::set<std::string> subdirectories;
      bool has_files = false;
    };
    std::map<std::string, Directory> directories;
    for (std::size_t index = 0; index < chart.nodes.size(); ++index)
    {
      const Comprendo::Node &node = chart.nodes[index];
      if (node.kind != Comprendo::NodeKind::File || node_parent[index] != kNone)
      {
        continue;
      }
      std::string path = parent_directory(node.file);
      directories[path].has_files = true;
      while (!path.empty())
      {
        std::string up = parent_directory(path);
        directories[up].subdirectories.insert(path);
        path = std::move(up);
      }
    }

    DirectoryGroups result;
    result.group_of_directory[""] = 0;
    const auto name_of = [](const std::string &path)
    { return path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1); };
    // Depth-first, in path order, so group indices do not depend on node order.
    std::vector<std::tuple<std::string, std::size_t, std::string>> stack;
    for (const std::string &top : directories[""].subdirectories | std::views::reverse)
    {
      stack.emplace_back(top, 0, "");
    }
    while (!stack.empty())
    {
      auto [path, parent, prefix] = std::move(stack.back());
      stack.pop_back();
      const Directory &directory = directories.at(path);
      std::string label = prefix + name_of(path);
      if (!directory.has_files && directory.subdirectories.size() == 1)
      {
        stack.emplace_back(*directory.subdirectories.begin(), parent, label + "/");
        continue;
      }
      const std::size_t group = hierarchy.groups.size();
      hierarchy.groups.push_back(
        {.key = Comprendo::fnv1a64("directory\x1f" + path), .label = label, .directory = path}
      );
      result.group_of_directory[path] = group;
      result.parent_of_group[group] = parent;
      for (const std::string &sub : directory.subdirectories | std::views::reverse)
      {
        stack.emplace_back(sub, group, "");
      }
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
    case Grouping::Cluster: return Grouping::File;
    case Grouping::File: return Grouping::Namespace;
    case Grouping::Namespace: return Grouping::Cluster;
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
    const DirectoryGroups directories = grouping == Grouping::File
      ? add_directory_groups(chart, node_parent, hierarchy)
      : DirectoryGroups {};

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
      if (grouping == Grouping::File && parent == kNone && chart.nodes[index].kind == NodeKind::File)
      {
        return directories.group_of_directory.at(parent_directory(chart.nodes[index].file));
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
      std::size_t parent = 0;
      if (group < node_groups)
      {
        parent = parent_group_of_node(hierarchy.groups[group].nodes.front());
      }
      else if (
        const auto directory = directories.parent_of_group.find(group);
        directory != directories.parent_of_group.end()
      )
      {
        parent = directory->second;
      }
      else
      {
        parent = clusters.parent_of_group.at(group);
      }
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
