/// \file
/// \brief Chart model, stable ids, and validation.

#include "Comprendo/Chart.hpp"

#include <cstdint>
#include <string>
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
} // namespace

namespace Comprendo
{
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

  ValidationResult
  validate(const Chart &chart)
  {
    ValidationResult result;
    const auto add_error = [&result](std::string message)
    {
      result.ok = false;
      result.errors.push_back(std::move(message));
    };

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

    std::unordered_set<std::uint64_t> cluster_ids;
    for (const Cluster &cluster : chart.clusters)
    {
      if (cluster.id == 0)
      {
        add_error("cluster id must be non-zero");
      }
      if (!cluster_ids.insert(cluster.id).second)
      {
        add_error(fmt::format("duplicate cluster id {}", cluster.id));
      }
    }

    for (const Node &node : chart.nodes)
    {
      if (node.cluster_id != 0 && cluster_ids.find(node.cluster_id) == cluster_ids.end())
      {
        add_error(fmt::format("node {} references missing cluster {}", node.id, node.cluster_id));
      }
    }

    for (const Edge &edge : chart.edges)
    {
      if (static_cast<std::uint8_t>(edge.kind) > static_cast<std::uint8_t>(EdgeKind::DataFlow))
      {
        add_error(fmt::format("edge {} -> {} has an out-of-range kind", edge.src, edge.dst));
      }
      if (static_cast<std::uint8_t>(edge.provenance) > static_cast<std::uint8_t>(Provenance::Model))
      {
        add_error(fmt::format("edge {} -> {} has an out-of-range provenance", edge.src, edge.dst));
      }
      if (node_ids.find(edge.src) == node_ids.end())
      {
        add_error(fmt::format("edge source {} does not exist", edge.src));
      }
      if (node_ids.find(edge.dst) == node_ids.end())
      {
        add_error(fmt::format("edge destination {} does not exist", edge.dst));
      }
    }

    if (!chart.layout.empty() && chart.layout.size() != chart.nodes.size())
    {
      add_error(fmt::format(
        "layout has {} positions for {} nodes", chart.layout.size(), chart.nodes.size()
      ));
    }

    return result;
  }
} // namespace Comprendo
