#pragma once

/// \file
/// \brief Grouping a chart into a tree of nested groups, by file or by namespace.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Comprendo/Chart.hpp"

namespace Comprendo
{
  /// How chart nodes are nested into groups.
  enum class Grouping : std::uint8_t
  {
    /// The stored `Contains` tree: file → namespace → class → member, with
    /// files nested in their directories. A directory whose only content is one
    /// subdirectory merges with it into one group labelled "a/b".
    File,
    /// Namespace nodes with equal qualified names are merged across files;
    /// file nodes become leaves of the root and their contents move to the root.
    Namespace,
    /// The chart's clusters (subsystems named by the model) as nested groups;
    /// each node moves into its cluster with its `Contains` subtree, members of
    /// other clusters excepted. Top-level nodes outside every cluster go into
    /// one "(unclustered)" group.
    Cluster,
  };

  /// Lower-case name of a grouping, as shown in the status bar.
  ///
  /// \param grouping Grouping.
  /// \return "file", "namespace", or "cluster".
  [[nodiscard]]
  std::string_view
  to_string(Grouping grouping);

  /// The grouping the `G` key switches to: cluster, file, namespace, cluster, ...
  ///
  /// \param grouping Current grouping.
  /// \return Next grouping.
  [[nodiscard]]
  Grouping
  next_grouping(Grouping grouping);

  /// One group of the hierarchy.
  struct HierarchyGroup
  {
    /// Stable key: a node id, a hash of a merged namespace name, or 0 for the root.
    std::uint64_t key = 0;
    /// Display label: the node name, the merged namespace's qualified name, or empty.
    std::string label;
    /// Indices of the chart nodes this group represents (several for a merged namespace).
    std::vector<std::size_t> nodes;
    /// Indices of child groups, sorted by key.
    std::vector<std::size_t> children;
    /// Index of the parent group; the root is its own parent.
    std::size_t parent = 0;
    /// Cluster id for groups standing for a cluster, otherwise 0.
    std::uint64_t cluster = 0;
    /// Repository-relative path for groups standing for a directory, otherwise empty.
    std::string directory;
  };

  /// A chart's nodes arranged as a tree of groups.
  struct Hierarchy
  {
    /// Groups; index 0 is the root, which represents no node.
    std::vector<HierarchyGroup> groups;
    /// For each chart node, the index of the group that represents it.
    std::vector<std::size_t> group_of_node;
  };

  /// Check whether a namespace name was invented by ctags for an anonymous namespace.
  ///
  /// \param name Unqualified namespace name.
  /// \return True for names starting with `__anon`.
  [[nodiscard]]
  bool
  is_anonymous_name(std::string_view name);

  /// Arrange a chart's nodes into nested groups.
  ///
  /// Each node's parent is its `Contains` source; with several, the smallest id
  /// wins, and parent chains that loop are cut, so the result is always a tree.
  /// With Grouping::Namespace, named namespaces merge by qualified name and are
  /// nested under their enclosing namespace, anonymous namespaces stay separate,
  /// and children of file nodes move to the root. With Grouping::Cluster, a node
  /// whose cluster (its own `cluster_id`, else its parent's) differs from its
  /// parent's moves under that cluster's group; cluster groups nest by
  /// `parent_id`. A chart without clusters groups like Grouping::File. The result does not depend
  /// on the order of nodes or edges in the chart.
  ///
  /// \param chart Chart to group.
  /// \param grouping Grouping to apply.
  /// \return The hierarchy.
  [[nodiscard]]
  Hierarchy
  build_hierarchy(const Chart &chart, Grouping grouping);
} // namespace Comprendo
