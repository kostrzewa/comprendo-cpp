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
  bool
  merges(const Comprendo::Node &node, Comprendo::Grouping grouping)
  {
    return grouping == Comprendo::Grouping::Namespace && node.kind == Comprendo::NodeKind::Namespace
      && !Comprendo::is_anonymous_name(node.name);
  }
} // namespace

namespace Comprendo
{
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

    const auto parent_group_of_node = [&](std::size_t index) -> std::size_t
    {
      const std::size_t parent = node_parent[index];
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
      const std::size_t parent = parent_group_of_node(hierarchy.groups[group].nodes.front());
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
