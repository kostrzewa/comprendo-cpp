#pragma once

/// \file
/// \brief Viewer logic without rendering: camera, level of detail, edges, and picking.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Comprendo/Chart.hpp"
#include "Comprendo/Hierarchy.hpp"
#include "Comprendo/Layout.hpp"
#include "Comprendo/QuadTree.hpp"

namespace Comprendo
{
  /// An sRGB colour.
  struct Rgb
  {
    /// Red channel.
    std::uint8_t r = 0;
    /// Green channel.
    std::uint8_t g = 0;
    /// Blue channel.
    std::uint8_t b = 0;

    /// Memberwise equality.

    ///

    /// \return True when all members are equal.
    bool
    operator==(const Rgb &) const = default;
  };

  /// Colour of a node kind on the viewer's dark background.
  ///
  /// \param kind Node kind.
  /// \return Distinct colour per kind; grey for Other and out-of-range values.
  [[nodiscard]]
  Rgb
  kind_color(NodeKind kind);

  /// An axis-aligned rectangle in screen pixels.
  struct ScreenRect
  {
    /// Left edge.
    float x = 0.0F;
    /// Top edge.
    float y = 0.0F;
    /// Width.
    float width = 0.0F;
    /// Height.
    float height = 0.0F;

    /// Memberwise equality.

    ///

    /// \return True when all members are equal.
    bool
    operator==(const ScreenRect &) const = default;
  };

  /// A 2D view onto chart coordinates.
  struct ViewCamera
  {
    /// Chart point shown at the centre of the screen.
    Vec2 target;
    /// Screen pixels per chart unit.
    float zoom = 1.0F;
    /// Screen width in pixels.
    float width = 1.0F;
    /// Screen height in pixels.
    float height = 1.0F;

    /// Convert a chart point to screen pixels.
    ///
    /// \param world Chart point.
    /// \return Screen point.
    [[nodiscard]]
    Vec2
    world_to_screen(const Vec2 &world) const;

    /// Convert a screen point to chart coordinates.
    ///
    /// \param screen Screen point.
    /// \return Chart point.
    [[nodiscard]]
    Vec2
    screen_to_world(const Vec2 &screen) const;

    /// Move the view so the content follows a drag of \p delta screen pixels.
    ///
    /// \param delta Mouse movement in screen pixels.
    void
    pan(const Vec2 &delta);

    /// Zoom by \p factor while keeping the chart point under \p anchor fixed.
    ///
    /// \param anchor Screen point that stays put, usually the mouse.
    /// \param factor Zoom multiplier, above 1 to zoom in.
    /// \param min_zoom Smallest allowed zoom.
    /// \param max_zoom Largest allowed zoom.
    void
    zoom_at(const Vec2 &anchor, float factor, float min_zoom, float max_zoom);

    /// Centre a disk in a screen area and scale it to fill that area.
    ///
    /// \param centre Disk centre in chart coordinates.
    /// \param radius Disk radius in chart units.
    /// \param margin Fraction of the area's smaller side left free around the disk.
    /// \param area Screen area to fill, for example the part not covered by panels.
    void
    fit(const Vec2 &centre, float radius, float margin, const ScreenRect &area);
  };

  /// Pixel thresholds of the level of detail.
  struct LodOptions
  {
    /// Groups smaller than this screen radius are drawn as points.
    float point_pixels = 1.5F;
    /// Containers smaller than this screen radius are drawn filled, without children.
    float expand_pixels = 8.0F;
    /// Extra screen distance within which small groups can still be picked.
    float pick_tolerance = 3.0F;
  };

  /// How a visible group is drawn.
  enum class DrawStyle : std::uint8_t
  {
    /// A single point: too small for a disk.
    Point,
    /// A filled disk: a leaf, or a container too small to show its children.
    Disk,
    /// An outlined disk whose children are drawn inside it.
    Container,
  };

  /// One group to draw.
  struct DrawItem
  {
    /// Hierarchy group index.
    std::size_t group = 0;
    /// Drawing style.
    DrawStyle style = DrawStyle::Disk;

    /// Memberwise equality.

    ///

    /// \return True when all members are equal.
    bool
    operator==(const DrawItem &) const = default;
  };

  /// An edge between two visible groups, aggregated from chart edges.
  struct VisibleEdge
  {
    /// Source group.
    std::size_t from = 0;
    /// Destination group.
    std::size_t to = 0;
    /// Edge kind.
    EdgeKind kind = EdgeKind::Other;
    /// Summed weight of the aggregated chart edges.
    float weight = 0.0F;

    /// Memberwise equality.

    ///

    /// \return True when all members are equal.
    bool
    operator==(const VisibleEdge &) const = default;
  };

  /// A chart prepared for viewing: hierarchy and layout for one grouping, plus
  /// the queries the renderer and the input handling need. Free of any graphics
  /// library, so it is tested directly.
  class ChartView
  {
  public:
    /// Group and lay out a chart.
    ///
    /// \param chart Chart to view.
    /// \param grouping Initial grouping.
    /// \param options Layout tuning parameters.
    ChartView(Chart chart, Grouping grouping, const LayoutOptions &options = {});

    /// Take over a re-read chart's annotations when its structure is unchanged.
    ///
    /// The structure is everything the hierarchy and layout depend on: node
    /// ids, kinds, names, files, lines, and signatures in the same order, and
    /// the same edges with the same weights. When it matches, summaries,
    /// details, clusters, provenance, edge evidence, and header fields are
    /// copied in and the layout is kept, so annotating does not move anything.
    ///
    /// \param chart Chart read again from disk.
    /// \return True when the annotations were taken over; false when the
    ///         structure differs and a new view is needed.
    [[nodiscard]]
    bool
    update_annotations(const Chart &chart);

    /// Switch grouping, rebuilding the hierarchy and the layout.
    ///
    /// \param grouping New grouping; nothing happens when it is unchanged.
    void
    set_grouping(Grouping grouping);

    /// Current grouping.
    ///
    /// \return Grouping in effect.
    [[nodiscard]]
    Grouping
    grouping() const
    {
      return m_grouping;
    }

    /// Viewed chart.
    ///
    /// \return The chart.
    [[nodiscard]]
    const Chart &
    chart() const
    {
      return m_chart;
    }

    /// Hierarchy of the current grouping.
    ///
    /// \return The hierarchy.
    [[nodiscard]]
    const Hierarchy &
    hierarchy() const
    {
      return m_hierarchy;
    }

    /// Layout of the current grouping.
    ///
    /// \return The layout.
    [[nodiscard]]
    const Layout &
    layout() const
    {
      return m_layout;
    }

    /// Kind shown for a group: its first node's kind, Other for the root.
    ///
    /// \param group Group index.
    /// \return Node kind.
    [[nodiscard]]
    NodeKind
    group_kind(std::size_t group) const;

    /// Colour of a group: its cluster's colour for cluster groups (a colour
    /// derived from the cluster id when the chart leaves it to the viewer),
    /// otherwise the colour of its kind.
    ///
    /// \param group Group index.
    /// \return Colour.
    [[nodiscard]]
    Rgb
    group_color(std::size_t group) const;

    /// Cluster a group's first node belongs to.
    ///
    /// \param group Group index.
    /// \return The cluster, or null when the group has no node or the node no cluster.
    [[nodiscard]]
    const Cluster *
    node_cluster(std::size_t group) const;

    /// Cluster a cluster group stands for.
    ///
    /// \param group Group index.
    /// \return The cluster, or null for groups that are not cluster groups.
    [[nodiscard]]
    const Cluster *
    group_cluster(std::size_t group) const;

    /// Label shown for a group: its hierarchy label, with ctags' invented names
    /// of anonymous namespaces replaced by "(anonymous)".
    ///
    /// \param group Group index.
    /// \return Display label.
    [[nodiscard]]
    std::string
    display_label(std::size_t group) const;

    /// Children of a group in display order: containers first, then by display
    /// label (case-insensitive), then by key. Cached per grouping.
    ///
    /// \param group Group index.
    /// \return Child group indices.
    [[nodiscard]]
    const std::vector<std::size_t> &
    display_children(std::size_t group) const
    {
      return m_display_children[group];
    }

    /// Find groups by name.
    ///
    /// Case-insensitive substring match on the display label and on the first
    /// node's qualified name. Ranked: exact label, label prefix, label substring,
    /// qualified-name substring; then shorter labels first, then by label.
    ///
    /// \param query Text to look for; empty finds nothing.
    /// \param limit Maximum number of results.
    /// \return Matching group indices, best first.
    [[nodiscard]]
    std::vector<std::size_t>
    search(std::string_view query, std::size_t limit) const;

    /// Chart edges touching one node.
    struct NodeEdges
    {
      /// Indices into Chart::edges of edges leaving the node.
      std::vector<std::size_t> outgoing;
      /// Indices into Chart::edges of edges arriving at the node.
      std::vector<std::size_t> incoming;
    };

    /// Edges touching a node, independent of the grouping.
    ///
    /// \param node Index into Chart::nodes.
    /// \return Outgoing and incoming edge indices in chart order.
    [[nodiscard]]
    const NodeEdges &
    node_edges(std::size_t node) const
    {
      return m_node_edges[node];
    }

    /// Index of a node by id.
    ///
    /// \param id Node id.
    /// \return Index into Chart::nodes, or nothing for unknown ids.
    [[nodiscard]]
    std::optional<std::size_t>
    node_index(std::uint64_t id) const;

    /// Centre of a group in chart coordinates.
    ///
    /// \param group Group index.
    /// \return Centre.
    [[nodiscard]]
    Vec2
    group_centre(std::size_t group) const;

    /// Groups to draw, parents before children.
    ///
    /// Walks the hierarchy from the root: disks outside the screen are culled
    /// with their subtrees, groups below LodOptions::point_pixels become points,
    /// and containers below LodOptions::expand_pixels are drawn filled without
    /// their children. The result therefore grows with the screen area, not the
    /// chart size. The root itself is never drawn.
    ///
    /// \param camera View.
    /// \param lod Level-of-detail thresholds.
    /// \return Items in drawing order.
    [[nodiscard]]
    std::vector<DrawItem>
    visible(const ViewCamera &camera, const LodOptions &lod = {}) const;

    /// Non-`Contains` edges between visible groups.
    ///
    /// Each chart edge is attached to the deepest visible ancestor-or-self of
    /// both endpoints; edges whose endpoints share that group or are not visible
    /// are dropped, and parallel edges of one kind are merged with summed weight.
    ///
    /// \param items Result of visible().
    /// \return Edges sorted by (from, to, kind).
    [[nodiscard]]
    std::vector<VisibleEdge>
    visible_edges(const std::vector<DrawItem> &items) const;

    /// Group under a screen point, consistent with visible().
    ///
    /// Descends from the root into the child disk containing the point (small
    /// disks get LodOptions::pick_tolerance extra pixels; the nearest centre
    /// wins), stopping at groups drawn as points or filled disks.
    ///
    /// \param camera View.
    /// \param screen Screen point.
    /// \param lod Level-of-detail thresholds.
    /// \return Group index, or nothing over empty space.
    [[nodiscard]]
    std::optional<std::size_t>
    pick(const ViewCamera &camera, const Vec2 &screen, const LodOptions &lod = {}) const;

  private:
    /// A non-`Contains` chart edge between groups.
    struct GroupEdge
    {
      /// Source group.
      std::size_t from = 0;
      /// Destination group.
      std::size_t to = 0;
      /// Edge kind.
      EdgeKind kind = EdgeKind::Other;
      /// Edge weight.
      float weight = 0.0F;
    };

    /// Rebuild hierarchy, layout, and group edges for the current grouping.
    void
    rebuild();

    /// Viewed chart.
    Chart m_chart;
    /// Current grouping.
    Grouping m_grouping;
    /// Layout tuning parameters.
    LayoutOptions m_options;
    /// Hierarchy of the current grouping.
    Hierarchy m_hierarchy;
    /// Layout of the current grouping.
    Layout m_layout;
    /// Non-`Contains` edges mapped to groups.
    std::vector<GroupEdge> m_edges;
    /// Children of every group in display order.
    std::vector<std::vector<std::size_t>> m_display_children;
    /// Node index by node id.
    std::unordered_map<std::uint64_t, std::size_t> m_node_of_id;
    /// Edges touching each node.
    std::vector<NodeEdges> m_node_edges;
  };
} // namespace Comprendo
