#pragma once

/// \file
/// \brief Chart model: nodes, edges, clusters, stable ids, and validation.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Comprendo
{
  /// Schema version written to and accepted from chart files.
  inline constexpr std::uint32_t kChartSchemaVersion = 1;

  /// Kind of a chart node.
  enum class NodeKind : std::uint8_t
  {
    /// Unclassified node.
    Other = 0,
    /// Source file.
    File = 1,
    /// Module or translation unit.
    Module = 2,
    /// Namespace.
    Namespace = 3,
    /// Class.
    Class = 4,
    /// Struct.
    Struct = 5,
    /// Union.
    Union = 6,
    /// Enum.
    Enum = 7,
    /// Free function.
    Function = 8,
    /// Member function.
    Method = 9,
    /// Data member.
    Field = 10,
    /// Variable.
    Variable = 11,
    /// Preprocessor macro.
    Macro = 12,
    /// Type alias or typedef.
    TypeAlias = 13,
  };

  /// Kind of a chart edge.
  enum class EdgeKind : std::uint8_t
  {
    /// Unclassified edge.
    Other = 0,
    /// Lexical containment.
    Contains = 1,
    /// Include dependency.
    Includes = 2,
    /// Call relationship.
    Calls = 3,
    /// Inheritance relationship.
    Inherits = 4,
    /// General use relationship.
    Uses = 5,
    /// Data-flow relationship.
    DataFlow = 6,
  };

  /// Origin of an annotation.
  enum class Provenance : std::uint8_t
  {
    /// Origin is unknown.
    Unknown = 0,
    /// Produced by a deterministic extractor.
    Extractor = 1,
    /// Produced by the model.
    Model = 2,
  };

  /// A node in the chart.
  struct Node
  {
    /// Stable node id.
    std::uint64_t id = 0;
    /// Node kind.
    NodeKind kind = NodeKind::Other;
    /// Unqualified name.
    std::string name;
    /// Fully qualified name.
    std::string qualified_name;
    /// Repository-relative file path.
    std::string file;
    /// First line, 1-based.
    std::uint32_t line_start = 0;
    /// Last line, 1-based.
    std::uint32_t line_end = 0;
    /// Declaration signature.
    std::string signature;
    /// One-line model explanation.
    std::string summary;
    /// Long-form model explanation.
    std::string detail;
    /// Owning cluster id, or 0 when unclustered.
    std::uint64_t cluster_id = 0;
    /// Lines of code.
    std::uint32_t loc = 0;
    /// Cyclomatic complexity, or 0 when unknown.
    std::uint32_t complexity = 0;
    /// Annotation provenance.
    Provenance provenance = Provenance::Unknown;

    /// Memberwise equality.

    ///

    /// \return True when all members are equal.
    bool
    operator==(const Node &) const = default;
  };

  /// An edge between two nodes.
  struct Edge
  {
    /// Source node id.
    std::uint64_t src = 0;
    /// Destination node id.
    std::uint64_t dst = 0;
    /// Edge kind.
    EdgeKind kind = EdgeKind::Other;
    /// Aggregated strength.
    float weight = 1.0F;
    /// File:line or short justification.
    std::string evidence;
    /// Annotation provenance.
    Provenance provenance = Provenance::Unknown;

    /// Memberwise equality.

    ///

    /// \return True when all members are equal.
    bool
    operator==(const Edge &) const = default;
  };

  /// A group of related nodes.
  struct Cluster
  {
    /// Cluster id, non-zero.
    std::uint64_t id = 0;
    /// Display label.
    std::string label;
    /// Model explanation.
    std::string summary;
    /// Packed 0xRRGGBB color.
    std::uint32_t color = 0;
    /// Parent cluster id, or 0 for top level.
    std::uint64_t parent_id = 0;

    /// Memberwise equality.

    ///

    /// \return True when all members are equal.
    bool
    operator==(const Cluster &) const = default;
  };

  /// A 2D position.
  struct Position
  {
    /// Horizontal coordinate.
    float x = 0.0F;
    /// Vertical coordinate.
    float y = 0.0F;

    /// Memberwise equality.

    ///

    /// \return True when all members are equal.
    bool
    operator==(const Position &) const = default;
  };

  /// A complete chart.
  struct Chart
  {
    /// Schema version.
    std::uint32_t schema_version = kChartSchemaVersion;
    /// Project name.
    std::string project;
    /// Git commit at generation time.
    std::string git_commit;
    /// ISO-8601 UTC generation timestamp.
    std::string generated_at;
    /// Generator description.
    std::string generator;
    /// Absolute source root.
    std::string source_root;
    /// Chart nodes.
    std::vector<Node> nodes;
    /// Chart edges.
    std::vector<Edge> edges;
    /// Chart clusters.
    std::vector<Cluster> clusters;
    /// Cached node positions, one per node when present.
    std::vector<Position> layout;

    /// Memberwise equality.

    ///

    /// \return True when all members are equal.
    bool
    operator==(const Chart &) const = default;
  };

  /// Result of chart validation.
  struct ValidationResult
  {
    /// Human-readable validation errors.
    std::vector<std::string> errors;

    /// Check whether validation passed.
    ///
    /// \return True when no errors were found.
    [[nodiscard]]
    bool
    ok() const
    {
      return errors.empty();
    }
  };

  /// Name of a node kind as listed in plan/chart-schema.md.
  ///
  /// \param kind Node kind.
  /// \return Name such as "Function", or "Invalid" for out-of-range values.
  [[nodiscard]]
  std::string_view
  to_string(NodeKind kind);

  /// Name of an edge kind as listed in plan/chart-schema.md.
  ///
  /// \param kind Edge kind.
  /// \return Name such as "Calls", or "Invalid" for out-of-range values.
  [[nodiscard]]
  std::string_view
  to_string(EdgeKind kind);

  /// Compute the 64-bit FNV-1a hash of a byte range.
  ///
  /// \param data Bytes to hash.
  /// \return FNV-1a hash.
  [[nodiscard]]
  std::uint64_t
  fnv1a64(std::string_view data);

  /// Compute a stable node id from its identity fields.
  ///
  /// \param kind Node kind.
  /// \param qualified_name Fully qualified name.
  /// \param file Repository-relative file path.
  /// \param signature Declaration signature.
  /// \return Stable 64-bit node id.
  [[nodiscard]]
  std::uint64_t
  make_node_id(
    NodeKind kind,
    std::string_view qualified_name,
    std::string_view file,
    std::string_view signature
  );

  /// Sort a chart into canonical order.
  ///
  /// Nodes and clusters are ordered by id and edges by (src, dst, kind). When the
  /// layout has one position per node, positions move with their nodes.
  ///
  /// \param chart Chart to reorder in place.
  void
  canonicalize(Chart &chart);

  /// Validate a chart's internal consistency.
  ///
  /// \param chart Chart to validate.
  /// \return Validation result with all detected errors.
  [[nodiscard]]
  ValidationResult
  validate(const Chart &chart);
} // namespace Comprendo
