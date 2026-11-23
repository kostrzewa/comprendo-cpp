/// \file
/// \brief Tests for the hierarchical Barnes–Hut layout.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "Comprendo/Chart.hpp"
#include "Comprendo/Hierarchy.hpp"
#include "Comprendo/Layout.hpp"
#include "Comprendo/Random.hpp"

namespace
{
  using Comprendo::Chart;
  using Comprendo::EdgeKind;
  using Comprendo::Grouping;
  using Comprendo::Hierarchy;
  using Comprendo::Layout;
  using Comprendo::NodeKind;

  /// Shape of a generated chart.
  struct ChartShape
  {
    /// Number of files.
    std::size_t files = 8;
    /// Classes per file.
    std::size_t classes = 3;
    /// Methods per class.
    std::size_t methods = 4;
    /// Number of distinct namespaces the files are spread over.
    std::size_t namespaces = 3;
    /// Generator seed for include and call edges.
    std::uint64_t seed = 1;
  };

  /// Generate a chart resembling extractor output: files containing per-file
  /// namespace nodes, classes, and methods, plus include and call edges.
  ///
  /// \param shape Size and seed.
  /// \return The chart in canonical order.
  Chart
  generated_chart(const ChartShape &shape)
  {
    Chart chart;
    Comprendo::SplitMix64 random {shape.seed};
    const auto add
      = [&chart](NodeKind kind, std::string name, std::string qualified, const std::string &file)
    {
      const std::uint64_t id = Comprendo::make_node_id(kind, qualified, file, "");
      chart.nodes.push_back(
        {.id = id,
         .kind = kind,
         .name = std::move(name),
         .qualified_name = std::move(qualified),
         .file = file}
      );
      return id;
    };
    const auto edge = [&chart](std::uint64_t src, std::uint64_t dst, EdgeKind kind)
    { chart.edges.push_back({.src = src, .dst = dst, .kind = kind}); };

    std::vector<std::uint64_t> file_ids;
    std::vector<std::uint64_t> method_ids;
    for (std::size_t f = 0; f < shape.files; ++f)
    {
      const std::string file = fmt::format("src/file{}.cpp", f);
      const std::uint64_t file_id = add(NodeKind::File, fmt::format("file{}.cpp", f), file, file);
      file_ids.push_back(file_id);
      const std::string ns = fmt::format("ns{}", f % shape.namespaces);
      const std::uint64_t ns_id = add(NodeKind::Namespace, ns, ns, file);
      edge(file_id, ns_id, EdgeKind::Contains);
      for (std::size_t c = 0; c < shape.classes; ++c)
      {
        const std::string cls = fmt::format("{}::C{}_{}", ns, f, c);
        const std::uint64_t class_id = add(NodeKind::Class, fmt::format("C{}_{}", f, c), cls, file);
        edge(ns_id, class_id, EdgeKind::Contains);
        for (std::size_t m = 0; m < shape.methods; ++m)
        {
          const std::uint64_t method_id
            = add(NodeKind::Method, fmt::format("m{}", m), fmt::format("{}::m{}", cls, m), file);
          edge(class_id, method_id, EdgeKind::Contains);
          method_ids.push_back(method_id);
        }
      }
    }
    for (std::size_t f = 1; f < shape.files; ++f)
    {
      edge(file_ids[f], file_ids[random.next() % f], EdgeKind::Includes);
    }
    for (std::size_t i = 0; i < method_ids.size(); ++i)
    {
      const std::uint64_t target = method_ids[random.next() % method_ids.size()];
      if (target != method_ids[i])
      {
        edge(method_ids[i], target, EdgeKind::Calls);
      }
    }
    Comprendo::canonicalize(chart);
    const auto duplicates = std::ranges::unique(
      chart.edges, {}, [](const Comprendo::Edge &e) { return std::tie(e.src, e.dst, e.kind); }
    );
    chart.edges.erase(duplicates.begin(), duplicates.end());
    return chart;
  }

  /// Distance between two positions.
  ///
  /// \param a First position.
  /// \param b Second position.
  /// \return Euclidean distance.
  double
  distance(const Comprendo::Position &a, const Comprendo::Position &b)
  {
    return std::hypot(static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y);
  }

  /// Problems found by check_invariants(), empty when the layout is sound.
  struct Violations
  {
    /// Non-finite node positions.
    std::size_t non_finite = 0;
    /// Children whose disk leaves the parent's disk.
    std::size_t escaping = 0;
    /// Overlapping sibling pairs.
    std::size_t overlapping = 0;
  };

  /// Check the geometric invariants every layout must satisfy.
  ///
  /// \param chart Laid-out chart.
  /// \param hierarchy Hierarchy used for the layout.
  /// \param layout Layout to check.
  /// \return Counts of violations.
  Violations
  check_invariants(const Chart &chart, const Hierarchy &hierarchy, const Layout &layout)
  {
    constexpr double kTolerance = 1e-2;
    Violations violations;
    for (std::size_t i = 0; i < chart.nodes.size(); ++i)
    {
      const auto &p = layout.nodes[i];
      violations.non_finite += std::isfinite(p.x) && std::isfinite(p.y) ? 0 : 1;
    }
    for (std::size_t group = 0; group < hierarchy.groups.size(); ++group)
    {
      const auto &parent = layout.groups[group];
      const auto &children = hierarchy.groups[group].children;
      for (std::size_t a = 0; a < children.size(); ++a)
      {
        const auto &first = layout.groups[children[a]];
        violations.escaping
          += distance(first.centre, parent.centre) + first.radius > parent.radius + kTolerance ? 1
                                                                                               : 0;
        for (std::size_t b = a + 1; b < children.size(); ++b)
        {
          const auto &second = layout.groups[children[b]];
          violations.overlapping += distance(first.centre, second.centre) + kTolerance
              < static_cast<double>(first.radius) + second.radius
            ? 1
            : 0;
        }
      }
    }
    return violations;
  }

  /// Render node positions as golden-file text: one "id x y" line per node,
  /// sorted by id, coordinates rounded to one decimal.
  ///
  /// \param chart Laid-out chart.
  /// \param layout Layout to render.
  /// \return Golden text.
  std::string
  golden_text(const Chart &chart, const Layout &layout)
  {
    std::map<std::uint64_t, std::string> lines;
    for (std::size_t i = 0; i < chart.nodes.size(); ++i)
    {
      lines[chart.nodes[i].id] = fmt::format(
        "{:016x} {:.1f} {:.1f}\n", chart.nodes[i].id, layout.nodes[i].x, layout.nodes[i].y
      );
    }
    std::string text;
    for (const auto &[id, line] : lines)
    {
      text += line;
    }
    return text;
  }
} // namespace

/// Every layout must be geometrically sound in both groupings: finite
/// positions, children inside their parent's disk, and no overlapping siblings.
/// Lays out a generated chart (8 files, 3 namespaces, classes, methods, include
/// and call edges) grouped by file and by namespace, and checks the invariants,
/// the output sizes, and that merged namespace nodes share one position.
TEST_CASE("layout satisfies geometric invariants", "[layout]")
{
  const Chart chart = generated_chart({});
  for (const Grouping grouping : {Grouping::File, Grouping::Namespace})
  {
    const Hierarchy hierarchy = Comprendo::build_hierarchy(chart, grouping);
    const Layout layout = Comprendo::compute_layout(chart, hierarchy);
    CHECK(layout.nodes.size() == chart.nodes.size());
    CHECK(layout.groups.size() == hierarchy.groups.size());
    const Violations violations = check_invariants(chart, hierarchy, layout);
    CHECK(violations.non_finite == 0);
    CHECK(violations.escaping == 0);
    CHECK(violations.overlapping == 0);
    for (const auto &group : hierarchy.groups)
    {
      for (const std::size_t node : group.nodes)
      {
        CHECK(layout.nodes[node] == layout.nodes[group.nodes.front()]);
      }
    }
  }
}

/// Layouts must be reproducible and independent of storage order, but still
/// respond to the seed. Lays out a generated chart and a copy with nodes and
/// edges reversed and checks that every node id gets the same position; then
/// changes the seed and checks that at least one position moves.
TEST_CASE("layout is deterministic and order independent", "[layout]")
{
  const Chart chart = generated_chart({});
  Chart reversed = chart;
  std::ranges::reverse(reversed.nodes);
  std::ranges::reverse(reversed.edges);

  const Layout forward
    = Comprendo::compute_layout(chart, Comprendo::build_hierarchy(chart, Grouping::File));
  const Layout backward
    = Comprendo::compute_layout(reversed, Comprendo::build_hierarchy(reversed, Grouping::File));
  CHECK(golden_text(chart, forward) == golden_text(reversed, backward));

  Comprendo::LayoutOptions reseeded;
  reseeded.seed = 12345;
  const Layout other
    = Comprendo::compute_layout(chart, Comprendo::build_hierarchy(chart, Grouping::File), reseeded);
  CHECK(golden_text(chart, forward) != golden_text(chart, other));
}

/// Edges should pull related groups together, otherwise the layout would only
/// reflect containment. Lays out twelve childless files where only files 0 and 1
/// are connected (by several include edges) and checks that their distance is
/// below the median distance of all other file pairs.
TEST_CASE("edges pull connected groups together", "[layout]")
{
  Chart chart;
  for (std::uint64_t f = 1; f <= 12; ++f)
  {
    chart.nodes.push_back({.id = f, .kind = NodeKind::File, .name = fmt::format("f{}", f)});
  }
  chart.edges.push_back({.src = 1, .dst = 2, .kind = EdgeKind::Includes, .weight = 4.0F});
  const Hierarchy hierarchy = Comprendo::build_hierarchy(chart, Grouping::File);
  const Layout layout = Comprendo::compute_layout(chart, hierarchy);

  std::vector<double> others;
  for (std::size_t a = 0; a < chart.nodes.size(); ++a)
  {
    for (std::size_t b = a + 1; b < chart.nodes.size(); ++b)
    {
      if (a != 0 || b != 1)
      {
        others.push_back(distance(layout.nodes[a], layout.nodes[b]));
      }
    }
  }
  std::ranges::nth_element(others, others.begin() + static_cast<std::ptrdiff_t>(others.size() / 2));
  CHECK(distance(layout.nodes[0], layout.nodes[1]) < others[others.size() / 2]);
}

/// Real codebases put thousands of symbols in one namespace, which is exactly
/// where Barnes–Hut matters. Lays out a file whose namespace holds one class with
/// 3000 methods and checks the invariants still hold, which exercises the
/// quadtree for repulsion and for overlap removal at that scale.
TEST_CASE("layout scales to thousands of siblings", "[layout]")
{
  const Chart chart = generated_chart({.files = 1, .classes = 1, .methods = 3000, .namespaces = 1});
  const Hierarchy hierarchy = Comprendo::build_hierarchy(chart, Grouping::File);
  const Layout layout = Comprendo::compute_layout(chart, hierarchy);
  const Violations violations = check_invariants(chart, hierarchy, layout);
  CHECK(violations.non_finite == 0);
  CHECK(violations.escaping == 0);
  CHECK(violations.overlapping == 0);
}

/// Golden files catch unintended changes to the algorithm or its constants,
/// which tests of invariants alone would miss. Lays out a small generated chart
/// in both groupings, renders positions rounded to 0.1 per node id, and compares
/// with tests/golden/layout_<grouping>.txt; set COMPRENDO_UPDATE_GOLDEN=1 to
/// rewrite the files after an intended change.
TEST_CASE("layout matches golden files", "[layout][golden]")
{
  const Chart chart = generated_chart({.files = 4, .classes = 2, .methods = 2, .namespaces = 2});
  for (const auto &[grouping, name] :
       {std::pair {Grouping::File, "file"}, std::pair {Grouping::Namespace, "namespace"}})
  {
    const Layout layout
      = Comprendo::compute_layout(chart, Comprendo::build_hierarchy(chart, grouping));
    const std::string actual = golden_text(chart, layout);
    const std::filesystem::path path
      = std::filesystem::path {COMPRENDO_GOLDEN_DIR} / fmt::format("layout_{}.txt", name);
    if (std::getenv("COMPRENDO_UPDATE_GOLDEN") != nullptr)
    {
      std::ofstream {path} << actual;
      WARN("rewrote " << path.string());
      continue;
    }
    std::ifstream stream {path};
    std::stringstream expected;
    expected << stream.rdbuf();
    INFO("golden file " << path.string());
    CHECK(actual == expected.str());
  }
}

/// Pressing C in the viewer re-centres the layout on the selection: it must
/// land exactly at the chart's centre, nothing may overlap or escape its
/// container, and groups off the selection's path must keep their own
/// arrangement. Centres a deep method of a generated chart and checks its
/// position, the invariants, the off-path groups' child offsets, determinism,
/// and that centring on the root returns the base layout.
TEST_CASE("centre_layout puts a group at the centre", "[layout]")
{
  const Chart chart = generated_chart({});
  const Hierarchy hierarchy = Comprendo::build_hierarchy(chart, Grouping::File);
  const Layout base = Comprendo::compute_layout(chart, hierarchy);
  // The deepest group: a method inside a class inside a namespace inside a file.
  std::size_t deepest = 0;
  std::size_t deepest_depth = 0;
  for (std::size_t group = 1; group < hierarchy.groups.size(); ++group)
  {
    std::size_t depth = 0;
    for (std::size_t at = group; at != 0; at = hierarchy.groups[at].parent)
    {
      ++depth;
    }
    if (depth > deepest_depth)
    {
      deepest = group;
      deepest_depth = depth;
    }
  }
  CHECK(deepest_depth >= 3);
  const Layout centred = Comprendo::centre_layout(chart, hierarchy, base, deepest);
  CHECK(centred.groups[deepest].centre == Comprendo::Position {.x = 0.0F, .y = 0.0F});
  const Violations violations = check_invariants(chart, hierarchy, centred);
  CHECK(violations.non_finite == 0);
  CHECK(violations.escaping == 0);
  CHECK(violations.overlapping == 0);

  std::vector<bool> on_path(hierarchy.groups.size(), false);
  for (std::size_t at = deepest; at != 0; at = hierarchy.groups[at].parent)
  {
    on_path[hierarchy.groups[at].parent] = true;
  }
  std::size_t moved_inside = 0;
  for (std::size_t group = 0; group < hierarchy.groups.size(); ++group)
  {
    if (on_path[group])
    {
      continue;
    }
    for (const std::size_t child : hierarchy.groups[group].children)
    {
      const double before_x
        = static_cast<double>(base.groups[child].centre.x) - base.groups[group].centre.x;
      const double before_y
        = static_cast<double>(base.groups[child].centre.y) - base.groups[group].centre.y;
      const double after_x
        = static_cast<double>(centred.groups[child].centre.x) - centred.groups[group].centre.x;
      const double after_y
        = static_cast<double>(centred.groups[child].centre.y) - centred.groups[group].centre.y;
      constexpr double kTolerance = 1e-2;
      moved_inside
        += std::abs(before_x - after_x) > kTolerance || std::abs(before_y - after_y) > kTolerance
        ? 1
        : 0;
    }
  }
  CHECK(moved_inside == 0);
  CHECK(Comprendo::centre_layout(chart, hierarchy, base, deepest) == centred);
  CHECK(Comprendo::centre_layout(chart, hierarchy, base, 0) == base);
}

/// The switch between layouts is animated by interpolating them, so the ends
/// must be the two layouts exactly and the middle half way. Blends a base and
/// a centred layout at 0, 1, and 0.5, and checks a mismatched pair falls back
/// to the target.
TEST_CASE("layouts blend for animation", "[layout]")
{
  const Chart chart = generated_chart({.files = 3});
  const Hierarchy hierarchy = Comprendo::build_hierarchy(chart, Grouping::File);
  const Layout from = Comprendo::compute_layout(chart, hierarchy);
  const Layout to = Comprendo::centre_layout(chart, hierarchy, from, hierarchy.groups.size() - 1);
  CHECK(Comprendo::blend_layouts(from, to, 0.0F) == from);
  CHECK(Comprendo::blend_layouts(from, to, 1.0F) == to);
  const Layout middle = Comprendo::blend_layouts(from, to, 0.5F);
  const std::size_t g = hierarchy.groups.size() - 1;
  CHECK(
    std::abs(middle.groups[g].centre.x - ((from.groups[g].centre.x + to.groups[g].centre.x) / 2.0F))
    < 1e-3F
  );
  CHECK(Comprendo::blend_layouts(Layout {}, to, 0.5F) == to);
}
