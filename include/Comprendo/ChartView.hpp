#pragma once

/// \file
/// \brief Viewer logic without rendering: camera, level of detail, edges, and picking.

#include <bitset>
#include <map>
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

  /// Node and edge kinds hidden from the graph.
  struct ViewFilter
  {
    /// Hidden node kinds, indexed by NodeKind value.
    std::bitset<kNodeKindCount> hidden_nodes;
    /// Hidden edge kinds, indexed by EdgeKind value.
    std::bitset<kEdgeKindCount> hidden_edges;
    /// True to draw every edge between its members' groups; false (default) to
    /// aggregate edges into lines between sibling groups (see visible_edges()).
    bool member_edges = false;

    /// Check whether a node kind is shown.
    ///
    /// \param kind Node kind.
    /// \return True unless the kind is hidden.
    [[nodiscard]]
    bool
    shows(NodeKind kind) const
    {
      return !hidden_nodes.test(static_cast<std::size_t>(kind));
    }

    /// Check whether an edge kind is shown.
    ///
    /// \param kind Edge kind.
    /// \return True unless the kind is hidden.
    [[nodiscard]]
    bool
    shows(EdgeKind kind) const
    {
      return !hidden_edges.test(static_cast<std::size_t>(kind));
    }
  };

  /// What a hierarchy group stands for, as far as connections are concerned.
  enum class GroupType : std::uint8_t
  {
    /// The root.
    Root,
    /// A cluster named by the model.
    Cluster,
    /// A directory of the file grouping.
    Directory,
    /// A file node.
    File,
    /// A namespace (one file's, or merged across files).
    Namespace,
    /// Any other symbol: class, function, field, ...
    Symbol,
    /// A synthetic group without nodes, such as "(unclustered)".
    Other,
  };

  /// Aggregated connection between a group and a group of the same type.
  struct GroupConnection
  {
    /// The other group.
    std::size_t group = 0;
    /// Edge kind.
    EdgeKind kind = EdgeKind::Other;
    /// Number of chart edges between the two groups' contents.
    std::size_t edges = 0;

    /// Memberwise equality.
    ///
    /// \return True when all members are equal.
    bool
    operator==(const GroupConnection &) const = default;
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
    /// Groups whose kind \p filter hides are skipped with their subtrees;
    /// cluster groups are never hidden.
    ///
    /// \param camera View.
    /// \param lod Level-of-detail thresholds.
    /// \param filter Hidden kinds.
    /// \return Items in drawing order.
    [[nodiscard]]
    std::vector<DrawItem>
    visible(
      const ViewCamera &camera, const LodOptions &lod = {}, const ViewFilter &filter = {}
    ) const;

    /// Non-`Contains` edges between visible groups.
    ///
    /// By default edges are aggregated: each chart edge is lifted to the two
    /// sibling groups directly below the deepest group containing both
    /// endpoints (two clusters, two subclusters of one cluster, two files of
    /// one directory, ...), so one line stands for all edges between their
    /// contents and edges inside a group appear only once that group is
    /// expanded; edges where one endpoint contains the other are dropped. With
    /// ViewFilter::member_edges the endpoints' own groups are used instead.
    ///
    /// Edges crossing the boundary of \p focus (one endpoint inside it) are
    /// refined by one level: both endpoints become their ancestor-or-self one
    /// level below \p focus, or stay themselves when shallower. Selecting a
    /// cluster thus shows its files' connections, a file its classes', and a
    /// leaf its own connections to the members it uses. Crossing edges to an
    /// ancestor of \p focus are dropped.
    ///
    /// Each endpoint is then attached to its deepest drawn ancestor-or-self, or
    /// to the off-screen group below an expanded parent (so lines to groups
    /// outside the view keep pointing at them); edges whose endpoints share
    /// that group are dropped, and parallel edges of one kind are merged with
    /// summed weight.
    ///
    /// \param items Result of visible().
    /// \param filter Hidden kinds and the edge display mode.
    /// \param focus Selected group whose crossing edges are refined.
    /// \return Edges sorted by (from, to, kind).
    [[nodiscard]]
    std::vector<VisibleEdge>
    visible_edges(
      const std::vector<DrawItem> &items,
      const ViewFilter &filter = {},
      std::optional<std::size_t> focus = std::nullopt
    ) const;

    /// What a group stands for.
    ///
    /// \param group Group index.
    /// \return The group's type.
    [[nodiscard]]
    GroupType
    group_type(std::size_t group) const;

    /// Connections between a group's contents and the rest of the chart,
    /// aggregated by counterpart.
    ///
    /// Every non-`Contains` edge with exactly one endpoint inside \p group is
    /// counted against the other endpoint's nearest ancestor-or-self of the
    /// same type as \p group (another cluster for a cluster, another file for
    /// a file, ...); when there is none, the other endpoint's file, else its
    /// top-level group. Results are cached until the hierarchy changes.
    ///
    /// \param group Group whose connections are wanted.
    /// \param outgoing True for edges leaving the group, false for edges entering it.
    /// \return Connections by (counterpart, kind), most edges first, then by
    ///         counterpart label.
    [[nodiscard]]
    const std::vector<GroupConnection> &
    connections(std::size_t group, bool outgoing) const;

    /// Check whether a group is another group or lies inside it.
    ///
    /// \param ancestor Possible ancestor.
    /// \param group Group to test.
    /// \return True when \p group is \p ancestor or one of its descendants.
    [[nodiscard]]
    bool
    contains(std::size_t ancestor, std::size_t group) const;

    /// Group under a screen point, consistent with visible().
    ///
    /// Descends from the root into the child disk containing the point (small
    /// disks get LodOptions::pick_tolerance extra pixels; the nearest centre
    /// wins), stopping at groups drawn as points or filled disks.
    ///
    /// \param camera View.
    /// \param screen Screen point.
    /// \param lod Level-of-detail thresholds.
    /// \param filter Hidden kinds; hidden groups cannot be picked.
    /// \return Group index, or nothing over empty space.
    [[nodiscard]]
    std::optional<std::size_t>
    pick(
      const ViewCamera &camera,
      const Vec2 &screen,
      const LodOptions &lod = {},
      const ViewFilter &filter = {}
    ) const;

    /// Check whether a group is hidden by a filter: its kind is hidden and it
    /// is neither the root nor a cluster group.
    ///
    /// \param group Group index.
    /// \param filter Hidden kinds.
    /// \return True when the group is hidden.
    [[nodiscard]]
    bool
    hidden(std::size_t group, const ViewFilter &filter) const;

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
      /// Sibling ancestor-or-self of `from` below the endpoints' deepest common
      /// group; 0 when one endpoint contains the other.
      std::size_t lifted_from = 0;
      /// Sibling ancestor-or-self of `to`, paired with `lifted_from`.
      std::size_t lifted_to = 0;
    };

    /// Ancestor-or-self of a group at a depth.
    ///
    /// \param group Group index.
    /// \param depth Depth below the root.
    /// \return The ancestor at \p depth, or \p group itself when it is shallower.
    [[nodiscard]]
    std::size_t
    ancestor_at_depth(std::size_t group, std::uint32_t depth) const;

    /// Depth of each group below the root (the root has depth 0).
    std::vector<std::uint32_t> m_depth;
    /// Results of connections(), keyed by (group, outgoing); cleared by rebuild().
    mutable std::map<std::pair<std::size_t, bool>, std::vector<GroupConnection>> m_connections;

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
  /// Maps between the chart and a small overview box in a corner of the graph area.
  struct Minimap
  {
    /// Overview box on screen.
    ScreenRect box;
    /// Chart point shown at the centre of the box.
    Vec2 centre;
    /// Screen pixels per chart unit inside the box.
    float scale = 1.0F;

    /// Convert a chart point to a point in the box.
    ///
    /// \param world Chart point.
    /// \return Screen point.
    [[nodiscard]]
    Vec2
    to_screen(const Vec2 &world) const;

    /// Convert a point in the box to a chart point.
    ///
    /// \param screen Screen point.
    /// \return Chart point.
    [[nodiscard]]
    Vec2
    to_world(const Vec2 &screen) const;

    /// Check whether a screen point lies in the box.
    ///
    /// \param screen Screen point.
    /// \return True inside the box, edges included.
    [[nodiscard]]
    bool
    contains(const Vec2 &screen) const;
  };

  /// Place the overview box in the bottom-right corner of the graph area,
  /// scaled so the chart's root disk fills it.
  ///
  /// \param view Chart view.
  /// \param area Graph area.
  /// \param size Box side in pixels; shrunk to fit small areas.
  /// \param margin Distance from the area's edges in pixels.
  /// \return The minimap.
  [[nodiscard]]
  Minimap
  make_minimap(const ChartView &view, const ScreenRect &area, float size, float margin);

  /// Move a camera so it shows a chart point at the centre of the graph area.
  ///
  /// \param camera Camera to move.
  /// \param world Chart point.
  /// \param area Graph area.
  void
  centre_on(ViewCamera &camera, const Vec2 &world, const ScreenRect &area);
} // namespace Comprendo
