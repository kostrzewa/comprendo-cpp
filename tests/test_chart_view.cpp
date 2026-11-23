/// \file
/// \brief Tests for the viewer logic: camera, level of detail, edges, picking, colours.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Comprendo/Chart.hpp"
#include "Comprendo/ChartView.hpp"

namespace
{
  using Comprendo::Chart;
  using Comprendo::ChartView;
  using Comprendo::DrawItem;
  using Comprendo::DrawStyle;
  using Comprendo::EdgeKind;
  using Comprendo::Grouping;
  using Comprendo::NodeKind;
  using Comprendo::Vec2;
  using Comprendo::ViewCamera;

  /// Screen size used by the tests.
  constexpr float kScreen = 1000.0F;

  /// Build a chart of two files, each with a namespace `ns` holding one class:
  /// `ns::A` (methods m1, m2) in a.cpp and `ns::B` (method n1) in b.cpp.
  /// b.cpp includes a.cpp, and n1 calls m1 and m2.
  ///
  /// \return The chart.
  Chart
  sample_chart()
  {
    Chart chart;
    const auto node
      = [&chart](
          std::uint64_t id, NodeKind kind, const char *name, const char *qualified, const char *file
        )
    {
      chart.nodes.push_back(
        {.id = id, .kind = kind, .name = name, .qualified_name = qualified, .file = file}
      );
    };
    node(1, NodeKind::File, "a.cpp", "a.cpp", "a.cpp");
    node(2, NodeKind::File, "b.cpp", "b.cpp", "b.cpp");
    node(3, NodeKind::Namespace, "ns", "ns", "a.cpp");
    node(4, NodeKind::Namespace, "ns", "ns", "b.cpp");
    node(5, NodeKind::Class, "A", "ns::A", "a.cpp");
    node(6, NodeKind::Class, "B", "ns::B", "b.cpp");
    node(7, NodeKind::Method, "m1", "ns::A::m1", "a.cpp");
    node(8, NodeKind::Method, "m2", "ns::A::m2", "a.cpp");
    node(9, NodeKind::Method, "n1", "ns::B::n1", "b.cpp");
    const auto edge = [&chart](std::uint64_t src, std::uint64_t dst, EdgeKind kind)
    { chart.edges.push_back({.src = src, .dst = dst, .kind = kind}); };
    edge(1, 3, EdgeKind::Contains);
    edge(2, 4, EdgeKind::Contains);
    edge(3, 5, EdgeKind::Contains);
    edge(4, 6, EdgeKind::Contains);
    edge(5, 7, EdgeKind::Contains);
    edge(5, 8, EdgeKind::Contains);
    edge(6, 9, EdgeKind::Contains);
    edge(2, 1, EdgeKind::Includes);
    edge(9, 7, EdgeKind::Calls);
    edge(9, 8, EdgeKind::Calls);
    return chart;
  }

  /// Group of the node with a given id.
  ///
  /// \param view View to search.
  /// \param id Node id.
  /// \return Group index.
  std::size_t
  group_of(const ChartView &view, std::uint64_t id)
  {
    const auto &nodes = view.chart().nodes;
    const auto it = std::ranges::find(nodes, id, &Comprendo::Node::id);
    return view.hierarchy().group_of_node.at(static_cast<std::size_t>(it - nodes.begin()));
  }

  /// A camera fitted to the whole chart.
  ///
  /// \param view View to fit.
  /// \return Camera showing the root disk.
  ViewCamera
  fitted(const ChartView &view)
  {
    ViewCamera camera {.width = kScreen, .height = kScreen};
    camera.fit(
      view.group_centre(0),
      view.layout().groups.at(0).radius,
      0.1F,
      {.x = 0.0F, .y = 0.0F, .width = kScreen, .height = kScreen}
    );
    return camera;
  }

  /// Distance between two points.
  ///
  /// \param a First point.
  /// \param b Second point.
  /// \return Euclidean distance.
  float
  distance(const Vec2 &a, const Vec2 &b)
  {
    return std::hypot(a.x - b.x, a.y - b.y);
  }
} // namespace

/// Every interaction converts between screen and chart coordinates, so the camera
/// must be exact and intuitive. Checks that world/screen conversion round-trips,
/// that zooming keeps the chart point under the mouse fixed, that panning moves
/// content with the drag, and that fit() centres a disk at the requested size,
/// both on the whole screen and inside a smaller area left free by panels.
TEST_CASE("camera converts, zooms, pans, and fits", "[view]")
{
  ViewCamera camera {
    .target = {.x = 10.0F, .y = -5.0F}, .zoom = 2.0F, .width = 800.0F, .height = 600.0F
  };
  const Vec2 world {.x = 37.0F, .y = 12.5F};
  CHECK(distance(camera.screen_to_world(camera.world_to_screen(world)), world) < 1e-4F);

  const Vec2 anchor {.x = 123.0F, .y = 456.0F};
  const Vec2 under_mouse = camera.screen_to_world(anchor);
  camera.zoom_at(anchor, 3.0F, 0.01F, 100.0F);
  CHECK(camera.zoom == 6.0F);
  CHECK(distance(camera.screen_to_world(anchor), under_mouse) < 1e-3F);
  camera.zoom_at(anchor, 1000.0F, 0.01F, 100.0F);
  CHECK(camera.zoom == 100.0F);

  const Vec2 before = camera.world_to_screen(world);
  camera.pan({.x = 15.0F, .y = -7.0F});
  const Vec2 after = camera.world_to_screen(world);
  CHECK(std::abs(after.x - before.x - 15.0F) < 1e-3F);
  CHECK(std::abs(after.y - before.y + 7.0F) < 1e-3F);

  camera.fit(
    {.x = 5.0F, .y = 5.0F}, 50.0F, 0.1F, {.x = 0.0F, .y = 0.0F, .width = 800.0F, .height = 600.0F}
  );
  CHECK(camera.target == Vec2 {.x = 5.0F, .y = 5.0F});
  CHECK(std::abs(camera.zoom * 100.0F - 540.0F) < 1e-3F);

  const Comprendo::ScreenRect panel_free {
    .x = 200.0F, .y = 20.0F, .width = 400.0F, .height = 300.0F
  };
  camera.fit({.x = 5.0F, .y = 5.0F}, 50.0F, 0.0F, panel_free);
  const Vec2 centre_on_screen = camera.world_to_screen({.x = 5.0F, .y = 5.0F});
  CHECK(std::abs(centre_on_screen.x - 400.0F) < 1e-3F);
  CHECK(std::abs(centre_on_screen.y - 170.0F) < 1e-3F);
  CHECK(std::abs(camera.zoom - 3.0F) < 1e-5F);
}

/// The renderer draws exactly what visible() returns, so level of detail must
/// show everything when zoomed in, collapse to points when zoomed out, and cull
/// off-screen content. Checks the fitted view draws every group with parents
/// before children, a far zoomed-out view draws only top-level points, and a
/// view aimed far away draws nothing.
TEST_CASE("level of detail follows zoom and culls", "[view]")
{
  const ChartView view {sample_chart(), Grouping::File};
  const std::vector<DrawItem> all = view.visible(fitted(view));
  CHECK(all.size() == view.hierarchy().groups.size() - 1);
  std::set<std::size_t> seen;
  bool parents_first = true;
  for (const DrawItem &item : all)
  {
    const std::size_t parent = view.hierarchy().groups.at(item.group).parent;
    parents_first = parents_first && (parent == 0 || seen.contains(parent));
    seen.insert(item.group);
  }
  CHECK(parents_first);

  ViewCamera far = fitted(view);
  far.zoom = 1e-4F;
  const std::vector<DrawItem> points = view.visible(far);
  CHECK(points.size() == view.hierarchy().groups.at(0).children.size());
  CHECK(
    std::ranges::all_of(points, [](const DrawItem &item) { return item.style == DrawStyle::Point; })
  );

  ViewCamera away = fitted(view);
  away.target = {.x = 1e6F, .y = 1e6F};
  CHECK(view.visible(away).empty());
}

/// Which connections are drawn must depend only on the hierarchy and the
/// selection, never on zoom or panning. Computes the edges once, then checks
/// the selection-refined and member-level edges name exact groups (the called
/// methods, the files of the include) and that the camera is not even an input:
/// the result is the same whatever is on screen, including hidden-class ends.
TEST_CASE("shown edges do not depend on the camera", "[view]")
{
  const ChartView view {sample_chart(), Grouping::File};
  const Comprendo::ViewFilter members {.member_edges = true};
  const auto has
    = [](const std::vector<Comprendo::VisibleEdge> &edges, std::size_t from, std::size_t to)
  {
    return std::ranges::any_of(
      edges, [&](const auto &edge) { return edge.from == from && edge.to == to; }
    );
  };
  const auto detailed = view.shown_edges(members);
  CHECK(has(detailed, group_of(view, 9), group_of(view, 7)));
  CHECK(has(detailed, group_of(view, 9), group_of(view, 8)));
  CHECK(has(detailed, group_of(view, 2), group_of(view, 1)));

  const auto selected = view.shown_edges({}, group_of(view, 9));
  CHECK(has(selected, group_of(view, 9), group_of(view, 7)));
  CHECK(has(selected, group_of(view, 9), group_of(view, 8)));

  Comprendo::ViewFilter no_methods {.member_edges = true};
  no_methods.hidden_nodes.set(static_cast<std::size_t>(NodeKind::Method));
  const auto collapsed = view.shown_edges(no_methods);
  const auto call = std::ranges::find(collapsed, EdgeKind::Calls, &Comprendo::VisibleEdge::kind);
  CHECK(call != collapsed.end());
  if (call != collapsed.end())
  {
    CHECK(call->from == group_of(view, 6));
    CHECK(call->to == group_of(view, 5));
    CHECK(call->weight == 2.0F);
  }
}

/// Clicks must select what the user sees: the deepest drawn group under the
/// mouse. Checks that the centre of method m1 picks m1 in the fitted view and
/// that empty space picks nothing. Then zooms out until class A is below the
/// expand threshold and checks the pick is the filled disk visible() draws over
/// m1 (an ancestor of m1), not the hidden method.
TEST_CASE("picking matches the drawn level of detail", "[view]")
{
  const ChartView view {sample_chart(), Grouping::File};
  const ViewCamera camera = fitted(view);
  const std::size_t m1 = group_of(view, 7);
  CHECK(view.pick(camera, camera.world_to_screen(view.group_centre(m1))) == m1);
  CHECK_FALSE(view.pick(camera, {.x = 1.0F, .y = 1.0F}).has_value());

  ViewCamera coarse = camera;
  coarse.zoom = 5.0F / view.layout().groups.at(group_of(view, 5)).radius;
  const auto picked = view.pick(coarse, coarse.world_to_screen(view.group_centre(m1)));
  CHECK(picked.has_value());
  if (!picked)
  {
    return;
  }
  CHECK(*picked != m1);
  std::size_t ancestor = m1;
  while (ancestor != 0 && ancestor != *picked)
  {
    ancestor = view.hierarchy().groups.at(ancestor).parent;
  }
  CHECK(ancestor == *picked);
  const auto items = view.visible(coarse);
  const auto drawn = std::ranges::find(items, *picked, &DrawItem::group);
  CHECK(drawn != items.end());
  CHECK((drawn != items.end() && drawn->style != DrawStyle::Container));
}

/// Kind colours are the main visual cue, so every kind needs its own colour and
/// unknown values a safe fallback. Collects the colours of all fourteen kinds,
/// checks they are pairwise distinct, and checks an out-of-range value gets the
/// colour of Other.
TEST_CASE("kind colours are distinct", "[view]")
{
  std::set<std::uint32_t> colours;
  for (std::uint8_t kind = 0; kind <= std::to_underlying(NodeKind::TypeAlias); ++kind)
  {
    const Comprendo::Rgb rgb = Comprendo::kind_color(static_cast<NodeKind>(kind));
    colours.insert((std::uint32_t {rgb.r} << 16U) | (std::uint32_t {rgb.g} << 8U) | rgb.b);
  }
  CHECK(colours.size() == 14);
  CHECK(
    Comprendo::kind_color(static_cast<NodeKind>(200)) == Comprendo::kind_color(NodeKind::Other)
  );
}

/// The View menu switches grouping at runtime, which must rebuild hierarchy and
/// layout consistently. Switches the sample chart to namespace grouping and
/// checks the two `ns` nodes share one group, the namespace group is drawn, and
/// the include edge between file nodes is still visible.
TEST_CASE("switching grouping rebuilds the view", "[view]")
{
  ChartView view {sample_chart(), Grouping::File};
  CHECK(group_of(view, 3) != group_of(view, 4));
  view.set_grouping(Grouping::Namespace);
  CHECK(view.grouping() == Grouping::Namespace);
  CHECK(group_of(view, 3) == group_of(view, 4));
  const auto items = view.visible(fitted(view));
  CHECK(std::ranges::find(items, group_of(view, 3), &DrawItem::group) != items.end());
  const auto edges = view.shown_edges();
  CHECK(std::ranges::count(edges, EdgeKind::Includes, &Comprendo::VisibleEdge::kind) == 1);
}

/// Anonymous namespaces carry ctags-invented names such as `__anon1f2e`, which
/// mean nothing to users. Adds an anonymous namespace to the sample chart and
/// checks its display label is "(anonymous)" while a named group keeps its name.
TEST_CASE("anonymous namespaces display as anonymous", "[view]")
{
  Chart chart = sample_chart();
  chart.nodes.push_back(
    {.id = 10,
     .kind = NodeKind::Namespace,
     .name = "__anon1f2e",
     .qualified_name = "__anon1f2e",
     .file = "a.cpp"}
  );
  chart.edges.push_back({.src = 1, .dst = 10, .kind = EdgeKind::Contains});
  const ChartView view {chart, Grouping::File};
  CHECK(view.display_label(group_of(view, 10)) == "(anonymous)");
  CHECK(view.display_label(group_of(view, 5)) == "A");
}

/// The Explorer lists children in display_children() order; hierarchy order is
/// by hash key, which would look random. Adds a childless file whose name sorts
/// between the two existing files and checks the root lists the containers first
/// (a.cpp, b.cpp) and then the leaf, and that class A lists m1 before m2.
TEST_CASE("explorer children are containers first, then by label", "[view]")
{
  Chart chart = sample_chart();
  chart.nodes.push_back(
    {.id = 11,
     .kind = NodeKind::File,
     .name = "aa.cpp",
     .qualified_name = "aa.cpp",
     .file = "aa.cpp"}
  );
  const ChartView view {chart, Grouping::File};
  std::vector<std::string> labels;
  for (const std::size_t child : view.display_children(0))
  {
    labels.push_back(view.display_label(child));
  }
  CHECK(labels == std::vector<std::string> {"a.cpp", "b.cpp", "aa.cpp"});
  CHECK(
    view.display_children(group_of(view, 5))
    == std::vector<std::size_t> {group_of(view, 7), group_of(view, 8)}
  );
}

/// The Explorer filter must put the most likely match first and be
/// case-insensitive. Searches the sample chart for "a" and "A" and checks the
/// ranking: exact label (class A), label prefix (a.cpp), then qualified-name
/// matches (m1, m2 via ns::A::...); also checks the limit and the empty query.
TEST_CASE("search ranks exact, prefix, substring, qualified matches", "[view]")
{
  const ChartView view {sample_chart(), Grouping::File};
  const std::vector<std::size_t> expected {
    group_of(view, 5), group_of(view, 1), group_of(view, 7), group_of(view, 8)
  };
  CHECK(view.search("a", 10) == expected);
  CHECK(view.search("A", 10) == expected);
  CHECK(view.search("a", 2) == std::vector<std::size_t> {expected[0], expected[1]});
  CHECK(view.search("", 10).empty());
  CHECK(view.search("zzz", 10).empty());
}

/// The Inspector lists a selection's incoming and outgoing edges, which needs a
/// per-node index independent of the grouping. Checks that method n1 has its two
/// calls as outgoing edges and its containment as the only incoming edge, and
/// that node ids map to indices (unknown ids to nothing).
TEST_CASE("node edges and ids are indexed", "[view]")
{
  const ChartView view {sample_chart(), Grouping::File};
  const auto n1 = view.node_index(9);
  CHECK(n1 == std::optional<std::size_t> {8});
  CHECK_FALSE(view.node_index(12345).has_value());
  if (!n1)
  {
    return;
  }
  const ChartView::NodeEdges &edges = view.node_edges(*n1);
  CHECK(edges.outgoing.size() == 2);
  CHECK(edges.incoming.size() == 1);
  for (const std::size_t e : edges.outgoing)
  {
    CHECK(view.chart().edges.at(e).kind == EdgeKind::Calls);
  }
  CHECK(view.chart().edges.at(edges.incoming.front()).kind == EdgeKind::Contains);
}

/// Live reload must not move anything when the plugin only adds explanations,
/// but must rebuild when the structure changes. Annotates a re-read copy of the
/// sample chart and checks the view takes the text, cluster, and evidence while
/// keeping its layout; then adds a node and changes an edge weight and checks
/// both are refused so the viewer builds a new view.
TEST_CASE("annotation-only reloads keep the layout", "[view]")
{
  ChartView view {sample_chart(), Grouping::File};
  const Comprendo::Layout layout_before = view.layout();

  Chart annotated = sample_chart();
  annotated.nodes[6].summary = "First method";
  annotated.nodes[6].detail = "Longer text";
  annotated.nodes[6].provenance = Comprendo::Provenance::Model;
  annotated.edges[8].evidence = "b.cpp:3";
  annotated.clusters.push_back({.id = 77, .label = "Core"});
  annotated.nodes[6].cluster_id = 77;
  CHECK(view.update_annotations(annotated));
  CHECK(view.chart().nodes[6].summary == "First method");
  CHECK(view.chart().nodes[6].cluster_id == 77);
  CHECK(view.chart().edges[8].evidence == "b.cpp:3");
  CHECK(view.chart().clusters.size() == 1);
  CHECK(view.layout() == layout_before);

  Chart grown = sample_chart();
  grown.nodes.push_back(
    {.id = 10,
     .kind = NodeKind::Method,
     .name = "m3",
     .qualified_name = "ns::A::m3",
     .file = "a.cpp"}
  );
  CHECK_FALSE(view.update_annotations(grown));
  Chart reweighted = sample_chart();
  reweighted.edges[8].weight = 4.0F;
  CHECK_FALSE(view.update_annotations(reweighted));
  CHECK(view.chart().nodes[6].summary == "First method");
}

/// The View → Show menu hides symbol and relationship kinds; hidden symbols must
/// disappear with their contents, not be pickable, and hidden relationships must
/// not be drawn. Fits the sample chart, hides classes and checks no class or
/// method is drawn or picked at a class centre, then hides calls and checks the
/// call edges vanish while the include edge stays.
TEST_CASE("view filters hide node and edge kinds", "[view]")
{
  const ChartView view {sample_chart(), Grouping::File};
  const ViewCamera camera = fitted(view);
  const Comprendo::LodOptions lod {.point_pixels = 0.0F, .expand_pixels = 0.0F};

  Comprendo::ViewFilter no_classes;
  no_classes.hidden_nodes.set(static_cast<std::size_t>(NodeKind::Class));
  const auto items = view.visible(camera, lod, no_classes);
  CHECK_FALSE(items.empty());
  for (const Comprendo::DrawItem &item : items)
  {
    CHECK(view.group_kind(item.group) != NodeKind::Class);
    CHECK(view.group_kind(item.group) != NodeKind::Method);
  }
  const std::size_t class_a = group_of(view, 5);
  const auto picked
    = view.pick(camera, camera.world_to_screen(view.group_centre(class_a)), lod, no_classes);
  CHECK(picked != std::optional<std::size_t> {class_a});
  CHECK(view.hidden(class_a, no_classes));
  CHECK_FALSE(view.hidden(0, no_classes));

  Comprendo::ViewFilter no_calls;
  no_calls.hidden_edges.set(static_cast<std::size_t>(EdgeKind::Calls));
  const auto edges = view.shown_edges(no_calls);
  CHECK(std::ranges::none_of(edges, [](const auto &edge) { return edge.kind == EdgeKind::Calls; }));
  CHECK(
    std::ranges::any_of(edges, [](const auto &edge) { return edge.kind == EdgeKind::Includes; })
  );
  CHECK(
    std::ranges::any_of(
      view.shown_edges(), [](const auto &edge) { return edge.kind == EdgeKind::Calls; }
    )
  );
}

/// The minimap must sit in the graph area's corner, show the whole chart, and
/// map clicks back to the chart so the camera can jump there. Builds a minimap
/// for an area, checks its box, that the root disk fits inside it, that screen
/// and chart conversions invert each other, and that centre_on() moves the
/// clicked chart point to the middle of the area.
TEST_CASE("minimap maps between overview and chart", "[view]")
{
  const ChartView view {sample_chart(), Grouping::File};
  const Comprendo::ScreenRect area {.x = 100.0F, .y = 50.0F, .width = 900.0F, .height = 600.0F};
  const Comprendo::Minimap minimap = Comprendo::make_minimap(view, area, 160.0F, 10.0F);
  CHECK(
    minimap.box
    == Comprendo::ScreenRect {.x = 830.0F, .y = 480.0F, .width = 160.0F, .height = 160.0F}
  );

  const Vec2 centre = minimap.to_screen(view.group_centre(0));
  CHECK(distance(centre, {.x = 910.0F, .y = 560.0F}) < 1e-3F);
  CHECK(view.layout().groups.front().radius * minimap.scale <= 80.0F + 1e-3F);
  const Vec2 corner {.x = 840.0F, .y = 600.0F};
  CHECK(distance(minimap.to_screen(minimap.to_world(corner)), corner) < 1e-3F);
  CHECK(minimap.contains(corner));
  CHECK_FALSE(minimap.contains({.x = 829.0F, .y = 600.0F}));

  ViewCamera camera = fitted(view);
  const Vec2 target = minimap.to_world(corner);
  Comprendo::centre_on(camera, target, area);
  CHECK(distance(camera.world_to_screen(target), {.x = 550.0F, .y = 350.0F}) < 1e-2F);

  const Comprendo::Minimap tiny
    = Comprendo::make_minimap(view, {.width = 120.0F, .height = 90.0F}, 160.0F, 0.0F);
  CHECK(tiny.box.width == 30.0F);
}

/// Thousands of member-to-member lines drown the chart, so by default edges
/// are summarized between sibling groups. In the fully expanded sample, checks
/// the two calls from n1 to A's methods become one call line of weight 2
/// between b.cpp and a.cpp, and checks contains() on the hierarchy the lines
/// are lifted along.
TEST_CASE("edges are aggregated between sibling groups by default", "[view]")
{
  const ChartView view {sample_chart(), Grouping::File};
  const auto aggregated = view.shown_edges();
  const auto call = std::ranges::find(aggregated, EdgeKind::Calls, &Comprendo::VisibleEdge::kind);
  CHECK(std::ranges::count(aggregated, EdgeKind::Calls, &Comprendo::VisibleEdge::kind) == 1);
  CHECK(call != aggregated.end());
  if (call != aggregated.end())
  {
    CHECK(call->from == group_of(view, 2));
    CHECK(call->to == group_of(view, 1));
    CHECK(call->weight == 2.0F);
  }
  CHECK(view.contains(group_of(view, 1), group_of(view, 7)));
  CHECK_FALSE(view.contains(group_of(view, 7), group_of(view, 1)));
  CHECK(view.contains(0, group_of(view, 9)));
}

/// Selecting a group must show its connections one level finer than the
/// aggregated view, following the hierarchy down to member detail at a leaf.
/// Clusters the sample (cluster, file, namespace, class, method) and selects,
/// in turn, b's cluster, b.cpp, class B, and method n1, checking the call lines
/// end at files, namespaces, methods, and finally the called methods.
TEST_CASE("selection refines its crossing edges by one level", "[view]")
{
  Chart chart = sample_chart();
  chart.clusters.push_back({.id = 100, .label = "Alpha"});
  chart.clusters.push_back({.id = 200, .label = "Beta"});
  chart.nodes.at(0).cluster_id = 100;
  chart.nodes.at(1).cluster_id = 200;
  const ChartView view {chart, Grouping::Cluster};
  const auto &groups = view.hierarchy().groups;
  const auto cluster = [&](std::uint64_t id)
  {
    return static_cast<std::size_t>(
      std::ranges::find(groups, id, &Comprendo::HierarchyGroup::cluster) - groups.begin()
    );
  };
  using Pair = std::pair<std::size_t, std::size_t>;
  const auto call_pairs = [&](std::optional<std::size_t> focus)
  {
    std::vector<std::pair<Pair, float>> pairs;
    for (const auto &edge : view.shown_edges({}, focus))
    {
      if (edge.kind == EdgeKind::Calls)
      {
        pairs.push_back({{edge.from, edge.to}, edge.weight});
      }
    }
    return pairs;
  };
  using Calls = std::vector<std::pair<Pair, float>>;

  CHECK(call_pairs(std::nullopt) == Calls {{{cluster(200), cluster(100)}, 2.0F}});
  CHECK(call_pairs(cluster(200)) == Calls {{{group_of(view, 2), group_of(view, 1)}, 2.0F}});
  CHECK(call_pairs(group_of(view, 2)) == Calls {{{group_of(view, 4), group_of(view, 3)}, 2.0F}});
  CHECK(
    call_pairs(group_of(view, 6))
    == Calls {
      {{group_of(view, 9), group_of(view, 7)}, 1.0F}, {{group_of(view, 9), group_of(view, 8)}, 1.0F}
    }
  );
  CHECK(call_pairs(group_of(view, 9)) == call_pairs(group_of(view, 6)));
}

/// The Inspector of a cluster or file lists what it connects to at its own
/// level (other clusters, other files) with counts, since member-level lists
/// of a large group are unreadable. Builds the sample chart with clusters for
/// each file, then checks the cluster and the file connections: b's two calls
/// and its include into a, nothing outgoing from a, the counterpart's type,
/// and that the types of groups are recognized.
TEST_CASE("connections aggregate by groups of the same type", "[view]")
{
  Chart chart = sample_chart();
  chart.clusters.push_back({.id = 100, .label = "Alpha"});
  chart.clusters.push_back({.id = 200, .label = "Beta"});
  chart.nodes.at(0).cluster_id = 100;
  chart.nodes.at(1).cluster_id = 200;
  const ChartView clustered {chart, Grouping::Cluster};
  const auto cluster_group = [&](std::uint64_t id)
  {
    const auto &groups = clustered.hierarchy().groups;
    return static_cast<std::size_t>(
      std::ranges::find(groups, id, &Comprendo::HierarchyGroup::cluster) - groups.begin()
    );
  };
  const std::size_t alpha = cluster_group(100);
  const std::size_t beta = cluster_group(200);
  CHECK(clustered.group_type(alpha) == Comprendo::GroupType::Cluster);
  CHECK(
    clustered.connections(beta, true)
    == std::vector<Comprendo::GroupConnection> {
      {.group = alpha, .kind = EdgeKind::Calls, .edges = 2},
      {.group = alpha, .kind = EdgeKind::Includes, .edges = 1},
    }
  );
  CHECK(clustered.connections(alpha, true).empty());
  CHECK(clustered.connections(alpha, false).size() == 2);

  const ChartView files {sample_chart(), Grouping::File};
  const std::size_t a_file = group_of(files, 1);
  const std::size_t b_file = group_of(files, 2);
  CHECK(files.group_type(b_file) == Comprendo::GroupType::File);
  CHECK(files.group_type(group_of(files, 5)) == Comprendo::GroupType::Symbol);
  CHECK(files.group_type(0) == Comprendo::GroupType::Root);
  const auto &incoming = files.connections(a_file, false);
  CHECK(incoming.size() == 2);
  CHECK(std::ranges::all_of(incoming, [&](const auto &c) { return c.group == b_file; }));
}

/// Lines inside every group at once drown the chart, so internal lines are
/// drawn only where the user is looking: at the top level and inside the
/// selection and the groups enclosing it. Adds a call between A's two methods
/// and checks the line m1 -> m2 is hidden with no selection or with another
/// part of the chart selected, and shown once class A, or a method in it, is
/// selected; member connections still show it regardless.
TEST_CASE("internal lines are drawn only along the selection's path", "[view]")
{
  Chart chart = sample_chart();
  chart.edges.push_back({.src = 7, .dst = 8, .kind = EdgeKind::Calls});
  const ChartView view {chart, Grouping::File};
  const std::size_t m1 = group_of(view, 7);
  const std::size_t m2 = group_of(view, 8);
  const auto shows_m1_m2
    = [&](const Comprendo::ViewFilter &filter, std::optional<std::size_t> focus)
  {
    return std::ranges::any_of(
      view.shown_edges(filter, focus),
      [&](const auto &edge) { return edge.from == m1 && edge.to == m2; }
    );
  };
  CHECK_FALSE(shows_m1_m2({}, std::nullopt));
  CHECK_FALSE(shows_m1_m2({}, group_of(view, 1)));
  CHECK_FALSE(shows_m1_m2({}, group_of(view, 6)));
  CHECK(shows_m1_m2({}, group_of(view, 5)));
  CHECK(shows_m1_m2({}, m1));
  CHECK(shows_m1_m2({.member_edges = true}, std::nullopt));
  const auto top = view.shown_edges();
  CHECK(
    std::ranges::any_of(
      top,
      [&](const auto &edge)
      { return edge.from == group_of(view, 2) && edge.to == group_of(view, 1); }
    )
  );
}
