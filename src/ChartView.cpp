/// \file
/// \brief Viewer logic without rendering: camera, level of detail, edges, and picking.

#include "Comprendo/ChartView.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Profiler/Profiler.hpp"

namespace
{
  using Comprendo::Rgb;
  using Comprendo::Vec2;

  /// Colours indexed by NodeKind, chosen to stay distinct on a dark background.
  constexpr std::array<Rgb, 14> kKindColors = {{
    {.r = 154, .g = 154, .b = 154}, // Other
    {.r = 120, .g = 138, .b = 170}, // File
    {.r = 111, .g = 143, .b = 217}, // Module
    {.r = 158, .g = 124, .b = 214}, // Namespace
    {.r = 230, .g = 140, .b = 80},  // Class
    {.r = 222, .g = 178, .b = 72},  // Struct
    {.r = 204, .g = 124, .b = 100}, // Union
    {.r = 82, .g = 186, .b = 170},  // Enum
    {.r = 92, .g = 162, .b = 232},  // Function
    {.r = 128, .g = 196, .b = 242}, // Method
    {.r = 128, .g = 196, .b = 110}, // Field
    {.r = 176, .g = 206, .b = 92},  // Variable
    {.r = 222, .g = 98, .b = 92},   // Macro
    {.r = 214, .g = 130, .b = 186}, // TypeAlias
  }};

  /// Lower-case ASCII letters, leaving other bytes (including UTF-8) unchanged.
  ///
  /// \param text Text to convert.
  /// \return Lower-cased copy.
  std::string
  lowercase(std::string_view text)
  {
    std::string result {text};
    for (char &c : result)
    {
      if (c >= 'A' && c <= 'Z')
      {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    return result;
  }

  /// Squared distance between two points.
  ///
  /// \param a First point.
  /// \param b Second point.
  /// \return |a - b|^2.
  float
  distance_squared(const Vec2 &a, const Vec2 &b)
  {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
  }
} // namespace

namespace Comprendo
{
  Rgb
  kind_color(NodeKind kind)
  {
    const auto index = static_cast<std::size_t>(std::to_underlying(kind));
    return index < kKindColors.size() ? kKindColors.at(index) : kKindColors.front();
  }

  Vec2
  ViewCamera::world_to_screen(const Vec2 &world) const
  {
    return {
      .x = ((world.x - target.x) * zoom) + (width / 2.0F),
      .y = ((world.y - target.y) * zoom) + (height / 2.0F),
    };
  }

  Vec2
  ViewCamera::screen_to_world(const Vec2 &screen) const
  {
    return {
      .x = ((screen.x - (width / 2.0F)) / zoom) + target.x,
      .y = ((screen.y - (height / 2.0F)) / zoom) + target.y,
    };
  }

  void
  ViewCamera::pan(const Vec2 &delta)
  {
    target.x -= delta.x / zoom;
    target.y -= delta.y / zoom;
  }

  void
  ViewCamera::zoom_at(const Vec2 &anchor, float factor, float min_zoom, float max_zoom)
  {
    const Vec2 before = screen_to_world(anchor);
    zoom = std::clamp(zoom * factor, min_zoom, max_zoom);
    const Vec2 after = screen_to_world(anchor);
    target.x += before.x - after.x;
    target.y += before.y - after.y;
  }

  void
  ViewCamera::fit(const Vec2 &centre, float radius, float margin, const ScreenRect &area)
  {
    const float side = std::min(area.width, area.height) * (1.0F - margin);
    zoom = radius > 0.0F && side > 0.0F ? side / (2.0F * radius) : 1.0F;
    const Vec2 area_centre {.x = area.x + (area.width / 2.0F), .y = area.y + (area.height / 2.0F)};
    target = {
      .x = centre.x - ((area_centre.x - (width / 2.0F)) / zoom),
      .y = centre.y - ((area_centre.y - (height / 2.0F)) / zoom),
    };
  }

  ChartView::ChartView(Chart chart, Grouping grouping, const LayoutOptions &options) :
    m_chart(std::move(chart)), m_grouping(grouping), m_options(options)
  {
    m_node_of_id.reserve(m_chart.nodes.size());
    for (std::size_t i = 0; i < m_chart.nodes.size(); ++i)
    {
      m_node_of_id.emplace(m_chart.nodes[i].id, i);
    }
    m_node_edges.resize(m_chart.nodes.size());
    for (std::size_t e = 0; e < m_chart.edges.size(); ++e)
    {
      const auto src = m_node_of_id.find(m_chart.edges[e].src);
      const auto dst = m_node_of_id.find(m_chart.edges[e].dst);
      if (src != m_node_of_id.end() && dst != m_node_of_id.end())
      {
        m_node_edges[src->second].outgoing.push_back(e);
        m_node_edges[dst->second].incoming.push_back(e);
      }
    }
    rebuild();
  }

  bool
  ChartView::update_annotations(const Chart &chart)
  {
    const auto same_node = [](const Node &a, const Node &b)
    {
      return a.id == b.id && a.kind == b.kind && a.name == b.name
        && a.qualified_name == b.qualified_name && a.file == b.file && a.line_start == b.line_start
        && a.line_end == b.line_end && a.signature == b.signature;
    };
    const auto same_edge = [](const Edge &a, const Edge &b)
    { return a.src == b.src && a.dst == b.dst && a.kind == b.kind && a.weight == b.weight; };
    if (
      !std::ranges::equal(m_chart.nodes, chart.nodes, same_node)
      || !std::ranges::equal(m_chart.edges, chart.edges, same_edge)
    )
    {
      return false;
    }
    std::vector<Position> layout = std::move(m_chart.layout);
    m_chart = chart;
    m_chart.layout = std::move(layout);
    return true;
  }

  std::optional<std::size_t>
  ChartView::node_index(std::uint64_t id) const
  {
    const auto found = m_node_of_id.find(id);
    return found == m_node_of_id.end() ? std::nullopt : std::optional<std::size_t> {found->second};
  }

  std::vector<std::size_t>
  ChartView::search(std::string_view query, std::size_t limit) const
  {
    const std::string needle = lowercase(query);
    if (needle.empty() || limit == 0)
    {
      return {};
    }
    struct Match
    {
      int rank = 0;
      std::size_t length = 0;
      std::string label;
      std::size_t group = 0;
    };
    std::vector<Match> matches;
    for (std::size_t group = 1; group < m_hierarchy.groups.size(); ++group)
    {
      std::string label = lowercase(display_label(group));
      const std::vector<std::size_t> &nodes = m_hierarchy.groups[group].nodes;
      int rank = -1;
      if (label == needle)
      {
        rank = 0;
      }
      else if (label.starts_with(needle))
      {
        rank = 1;
      }
      else if (label.contains(needle))
      {
        rank = 2;
      }
      else if (
        !nodes.empty() && lowercase(m_chart.nodes[nodes.front()].qualified_name).contains(needle)
      )
      {
        rank = 3;
      }
      if (rank >= 0)
      {
        matches.push_back(
          {.rank = rank, .length = label.size(), .label = std::move(label), .group = group}
        );
      }
    }
    const auto key = [](const Match &m) { return std::tie(m.rank, m.length, m.label, m.group); };
    const std::size_t keep = std::min(limit, matches.size());
    std::ranges::partial_sort(
      matches, matches.begin() + static_cast<std::ptrdiff_t>(keep), {}, key
    );
    std::vector<std::size_t> result;
    result.reserve(keep);
    for (std::size_t i = 0; i < keep; ++i)
    {
      result.push_back(matches[i].group);
    }
    return result;
  }

  void
  ChartView::set_grouping(Grouping grouping)
  {
    if (grouping != m_grouping)
    {
      m_grouping = grouping;
      rebuild();
    }
  }

  void
  ChartView::rebuild()
  {
    const ProfileScope profile {"chart_view_rebuild"};
    m_hierarchy = build_hierarchy(m_chart, m_grouping);
    m_layout = compute_layout(m_chart, m_hierarchy, m_options);

    m_display_children.assign(m_hierarchy.groups.size(), {});
    std::vector<std::string> sort_label(m_hierarchy.groups.size());
    for (std::size_t group = 0; group < m_hierarchy.groups.size(); ++group)
    {
      sort_label[group] = lowercase(display_label(group));
    }
    for (std::size_t group = 0; group < m_hierarchy.groups.size(); ++group)
    {
      std::vector<std::size_t> &children = m_display_children[group];
      children = m_hierarchy.groups[group].children;
      std::ranges::sort(
        children,
        [&](std::size_t left, std::size_t right)
        {
          const bool left_leaf = m_hierarchy.groups[left].children.empty();
          const bool right_leaf = m_hierarchy.groups[right].children.empty();
          if (left_leaf != right_leaf)
          {
            return right_leaf;
          }
          if (sort_label[left] != sort_label[right])
          {
            return sort_label[left] < sort_label[right];
          }
          return m_hierarchy.groups[left].key < m_hierarchy.groups[right].key;
        }
      );
    }

    m_edges.clear();
    for (const Edge &edge : m_chart.edges)
    {
      const auto src = m_node_of_id.find(edge.src);
      const auto dst = m_node_of_id.find(edge.dst);
      if (edge.kind != EdgeKind::Contains && src != m_node_of_id.end() && dst != m_node_of_id.end())
      {
        m_edges.push_back({
          .from = m_hierarchy.group_of_node[src->second],
          .to = m_hierarchy.group_of_node[dst->second],
          .kind = edge.kind,
          .weight = edge.weight,
        });
      }
    }
  }

  NodeKind
  ChartView::group_kind(std::size_t group) const
  {
    const std::vector<std::size_t> &nodes = m_hierarchy.groups[group].nodes;
    return nodes.empty() ? NodeKind::Other : m_chart.nodes[nodes.front()].kind;
  }

  std::string
  ChartView::display_label(std::size_t group) const
  {
    const std::string &label = m_hierarchy.groups[group].label;
    return is_anonymous_name(label) ? std::string {"(anonymous)"} : label;
  }

  Vec2
  ChartView::group_centre(std::size_t group) const
  {
    const Position &centre = m_layout.groups[group].centre;
    return {.x = centre.x, .y = centre.y};
  }

  std::vector<DrawItem>
  ChartView::visible(const ViewCamera &camera, const LodOptions &lod) const
  {
    std::vector<DrawItem> items;
    std::vector<std::size_t> stack(
      m_hierarchy.groups.front().children.rbegin(), m_hierarchy.groups.front().children.rend()
    );
    while (!stack.empty())
    {
      const std::size_t group = stack.back();
      stack.pop_back();
      const float radius = m_layout.groups[group].radius * camera.zoom;
      const Vec2 centre = camera.world_to_screen(group_centre(group));
      if (
        centre.x + radius < 0.0F || centre.x - radius > camera.width || centre.y + radius < 0.0F
        || centre.y - radius > camera.height
      )
      {
        continue;
      }
      const std::vector<std::size_t> &children = m_hierarchy.groups[group].children;
      if (radius < lod.point_pixels)
      {
        items.push_back({.group = group, .style = DrawStyle::Point});
      }
      else if (children.empty() || radius < lod.expand_pixels)
      {
        items.push_back({.group = group, .style = DrawStyle::Disk});
      }
      else
      {
        items.push_back({.group = group, .style = DrawStyle::Container});
        stack.insert(stack.end(), children.rbegin(), children.rend());
      }
    }
    return items;
  }

  std::vector<VisibleEdge>
  ChartView::visible_edges(const std::vector<DrawItem> &items) const
  {
    std::vector<bool> drawn(m_hierarchy.groups.size(), false);
    for (const DrawItem &item : items)
    {
      drawn[item.group] = true;
    }
    const auto representative = [&](std::size_t group)
    {
      while (group != 0 && !drawn[group])
      {
        group = m_hierarchy.groups[group].parent;
      }
      return group;
    };

    std::vector<VisibleEdge> edges;
    for (const GroupEdge &edge : m_edges)
    {
      const std::size_t from = representative(edge.from);
      const std::size_t to = representative(edge.to);
      if (from != 0 && to != 0 && from != to)
      {
        edges.push_back({.from = from, .to = to, .kind = edge.kind, .weight = edge.weight});
      }
    }
    const auto key
      = [](const VisibleEdge &edge) { return std::tuple {edge.from, edge.to, edge.kind}; };
    std::ranges::sort(edges, {}, key);
    std::vector<VisibleEdge> merged;
    for (const VisibleEdge &edge : edges)
    {
      if (!merged.empty() && key(merged.back()) == key(edge))
      {
        merged.back().weight += edge.weight;
      }
      else
      {
        merged.push_back(edge);
      }
    }
    return merged;
  }

  std::optional<std::size_t>
  ChartView::pick(const ViewCamera &camera, const Vec2 &screen, const LodOptions &lod) const
  {
    const Vec2 point = camera.screen_to_world(screen);
    const float tolerance = lod.pick_tolerance / camera.zoom;
    std::size_t current = 0;
    while (true)
    {
      std::optional<std::size_t> best;
      float best_distance = 0.0F;
      for (const std::size_t child : m_hierarchy.groups[current].children)
      {
        const float reach = std::max(m_layout.groups[child].radius, tolerance);
        const float distance = distance_squared(point, group_centre(child));
        if (distance <= reach * reach && (!best || distance < best_distance))
        {
          best = child;
          best_distance = distance;
        }
      }
      if (!best)
      {
        break;
      }
      current = *best;
      const float radius = m_layout.groups[current].radius * camera.zoom;
      if (radius < lod.expand_pixels || m_hierarchy.groups[current].children.empty())
      {
        break;
      }
    }
    return current == 0 ? std::nullopt : std::optional<std::size_t> {current};
  }
} // namespace Comprendo
