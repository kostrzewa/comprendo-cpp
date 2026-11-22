/// \file
/// \brief Chart model, stable ids, and validation.

#include "Comprendo/Chart.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <fmt/format.h>

namespace
{
  /// FNV-1a 64-bit offset basis.
  constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL;

  /// FNV-1a 64-bit prime.
  constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

  /// Field separator used when building node identity strings.
  constexpr char kSeparator = '\x1f';

  /// NodeKind names indexed by value.
  constexpr std::array<std::string_view, 14> kNodeKindNames = {
    "Other",
    "File",
    "Module",
    "Namespace",
    "Class",
    "Struct",
    "Union",
    "Enum",
    "Function",
    "Method",
    "Field",
    "Variable",
    "Macro",
    "TypeAlias"
  };

  /// EdgeKind names indexed by value.
  constexpr std::array<std::string_view, 7> kEdgeKindNames
    = {"Other", "Contains", "Includes", "Calls", "Inherits", "Uses", "DataFlow"};

  /// Look up the name of an enum value in a table.
  ///
  /// \param names Names indexed by value.
  /// \param value Enum value.
  /// \return The name, or "Invalid" when out of range.
  template <std::size_t N>
  std::string_view
  kind_name(const std::array<std::string_view, N> &names, std::uint8_t value)
  {
    return value < names.size() ? names.at(value) : std::string_view {"Invalid"};
  }
} // namespace

namespace Comprendo
{
  std::string_view
  to_string(NodeKind kind)
  {
    return kind_name(kNodeKindNames, std::to_underlying(kind));
  }

  std::string_view
  to_string(EdgeKind kind)
  {
    return kind_name(kEdgeKindNames, std::to_underlying(kind));
  }

  std::uint64_t
  fnv1a64(std::string_view data)
  {
    std::uint64_t hash = kFnvOffset;
    for (const unsigned char byte : data)
    {
      hash ^= byte;
      hash *= kFnvPrime;
    }
    return hash;
  }

  std::uint64_t
  make_node_id(
    NodeKind kind,
    std::string_view qualified_name,
    std::string_view file,
    std::string_view signature
  )
  {
    std::string identity;
    identity.reserve(qualified_name.size() + file.size() + signature.size() + 4);
    identity.push_back(static_cast<char>(kind));
    identity.push_back(kSeparator);
    identity.append(qualified_name);
    identity.push_back(kSeparator);
    identity.append(file);
    identity.push_back(kSeparator);
    identity.append(signature);
    return fnv1a64(identity);
  }

  void
  canonicalize(Chart &chart)
  {
    if (chart.layout.size() == chart.nodes.size())
    {
      std::vector<std::size_t> order(chart.nodes.size());
      std::ranges::iota(order, std::size_t {0});
      std::ranges::sort(order, {}, [&chart](std::size_t index) { return chart.nodes[index].id; });
      std::vector<Node> nodes;
      std::vector<Position> layout;
      nodes.reserve(order.size());
      layout.reserve(order.size());
      for (const std::size_t index : order)
      {
        nodes.push_back(std::move(chart.nodes[index]));
        layout.push_back(chart.layout[index]);
      }
      chart.nodes = std::move(nodes);
      chart.layout = std::move(layout);
    }
    else
    {
      std::ranges::sort(chart.nodes, {}, &Node::id);
    }
    std::ranges::sort(chart.clusters, {}, &Cluster::id);
    std::ranges::sort(
      chart.edges, {}, [](const Edge &edge) { return std::tuple {edge.src, edge.dst, edge.kind}; }
    );
  }

  ValidationResult
  validate(const Chart &chart)
  {
    ValidationResult result;
    const auto add_error
      = [&result](std::string message) { result.errors.push_back(std::move(message)); };

    if (chart.schema_version != kChartSchemaVersion)
    {
      add_error(fmt::format("unsupported schema version {}", chart.schema_version));
    }

    std::unordered_set<std::uint64_t> node_ids;
    for (const Node &node : chart.nodes)
    {
      if (!node_ids.insert(node.id).second)
      {
        add_error(fmt::format("duplicate node id {}", node.id));
      }
      if (static_cast<std::uint8_t>(node.kind) > static_cast<std::uint8_t>(NodeKind::TypeAlias))
      {
        add_error(fmt::format("node {} has an out-of-range kind", node.id));
      }
      if (static_cast<std::uint8_t>(node.provenance) > static_cast<std::uint8_t>(Provenance::Model))
      {
        add_error(fmt::format("node {} has an out-of-range provenance", node.id));
      }
    }

    std::unordered_map<std::uint64_t, std::uint64_t> cluster_parents;
    for (const Cluster &cluster : chart.clusters)
    {
      if (cluster.id == 0)
      {
        add_error("cluster id must be non-zero");
      }
      if (!cluster_parents.emplace(cluster.id, cluster.parent_id).second)
      {
        add_error(fmt::format("duplicate cluster id {}", cluster.id));
      }
    }

    for (const Cluster &cluster : chart.clusters)
    {
      if (cluster.parent_id != 0 && !cluster_parents.contains(cluster.parent_id))
      {
        add_error(
          fmt::format("cluster {} references missing parent {}", cluster.id, cluster.parent_id)
        );
        continue;
      }
      std::uint64_t ancestor = cluster.parent_id;
      for (std::size_t steps = 0; ancestor != 0 && steps <= cluster_parents.size(); ++steps)
      {
        if (ancestor == cluster.id)
        {
          add_error(fmt::format("cluster {} is its own ancestor", cluster.id));
          break;
        }
        const auto parent = cluster_parents.find(ancestor);
        ancestor = parent == cluster_parents.end() ? 0 : parent->second;
      }
    }

    for (const Node &node : chart.nodes)
    {
      if (node.cluster_id != 0 && !cluster_parents.contains(node.cluster_id))
      {
        add_error(fmt::format("node {} references missing cluster {}", node.id, node.cluster_id));
      }
    }

    std::set<std::tuple<std::uint64_t, std::uint64_t, EdgeKind>> edge_keys;
    for (const Edge &edge : chart.edges)
    {
      if (!edge_keys.emplace(edge.src, edge.dst, edge.kind).second)
      {
        add_error(fmt::format("duplicate edge {} -> {} of the same kind", edge.src, edge.dst));
      }
      if (static_cast<std::uint8_t>(edge.kind) > static_cast<std::uint8_t>(EdgeKind::DataFlow))
      {
        add_error(fmt::format("edge {} -> {} has an out-of-range kind", edge.src, edge.dst));
      }
      if (static_cast<std::uint8_t>(edge.provenance) > static_cast<std::uint8_t>(Provenance::Model))
      {
        add_error(fmt::format("edge {} -> {} has an out-of-range provenance", edge.src, edge.dst));
      }
      if (!node_ids.contains(edge.src))
      {
        add_error(fmt::format("edge source {} does not exist", edge.src));
      }
      if (!node_ids.contains(edge.dst))
      {
        add_error(fmt::format("edge destination {} does not exist", edge.dst));
      }
    }

    if (!chart.layout.empty() && chart.layout.size() != chart.nodes.size())
    {
      add_error(
        fmt::format("layout has {} positions for {} nodes", chart.layout.size(), chart.nodes.size())
      );
    }

    return result;
  }
} // namespace Comprendo
