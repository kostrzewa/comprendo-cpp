/// \file
/// \brief raylib drawing of a ChartView: groups, edges, highlights, and labels.

#include "Comprendo/GraphRenderer.hpp"

#include <raylib.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
  using Comprendo::ChartView;
  using Comprendo::DrawItem;
  using Comprendo::DrawStyle;
  using Comprendo::EdgeKind;
  using Comprendo::ViewCamera;

  /// Opacity of container fills.
  constexpr float kContainerFill = 0.07F;
  /// Opacity of container rings.
  constexpr float kContainerRing = 0.55F;
  /// Opacity of edges.
  constexpr float kEdgeAlpha = 0.45F;
  /// Edge line width in pixels.
  constexpr float kEdgeWidth = 1.0F;
  /// Gap in pixels between a disk and its highlight ring.
  constexpr float kHighlightGap = 2.0F;
  /// Leaf screen radius from which a label is drawn.
  constexpr float kLeafLabelPixels = 10.0F;
  /// Container screen radius from which a label is drawn.
  constexpr float kContainerLabelPixels = 28.0F;
  /// Label font size in pixels.
  constexpr int kLabelSize = 12;
  /// Maximum labels drawn per frame.
  constexpr std::size_t kMaxLabels = 600;
  /// Padding in pixels kept between labels.
  constexpr float kLabelPadding = 2.0F;
  /// Colour of the selection ring.
  constexpr Color kSelectedColor {.r = 255, .g = 214, .b = 64, .a = 255};
  /// Colour of the hover ring.
  constexpr Color kHoveredColor {.r = 255, .g = 255, .b = 255, .a = 200};
  /// Colour of labels.
  constexpr Color kLabelColor {.r = 230, .g = 230, .b = 236, .a = 255};
  /// Colour of label shadows.
  constexpr Color kShadowColor {.r = 10, .g = 10, .b = 14, .a = 200};

  /// Fewest segments used for a circle.
  constexpr int kMinSegments = 24;
  /// Most segments used for a circle, enough for sub-pixel accuracy up to a
  /// radius of about 1e5 pixels.
  constexpr int kMaxSegments = 720;
  /// Screen radius covered per segment.
  constexpr float kPixelsPerSegment = 3.0F;

  /// Segments needed for a circle of a given screen radius to look round.
  ///
  /// \param radius Screen radius in pixels.
  /// \return Segment count.
  int
  circle_segments(float radius)
  {
    return std::clamp(static_cast<int>(radius / kPixelsPerSegment), kMinSegments, kMaxSegments);
  }

  /// Draw a filled circle with a segment count suited to its size.
  ///
  /// \param centre Screen centre.
  /// \param radius Screen radius.
  /// \param color Fill colour.
  void
  fill_circle(Vector2 centre, float radius, Color color)
  {
    DrawCircleSector(centre, radius, 0.0F, 360.0F, circle_segments(radius), color);
  }

  /// Draw a circle outline with a segment count suited to its size.
  ///
  /// \param centre Screen centre.
  /// \param radius Screen radius.
  /// \param color Line colour.
  void
  outline_circle(Vector2 centre, float radius, Color color)
  {
    DrawCircleSectorLines(centre, radius, 0.0F, 360.0F, circle_segments(radius), color);
  }

  /// raylib colour of a group's kind.
  ///
  /// \param view Chart view.
  /// \param group Group index.
  /// \return Opaque colour.
  Color
  group_color(const ChartView &view, std::size_t group)
  {
    const Comprendo::Rgb rgb = Comprendo::kind_color(view.group_kind(group));
    return {.r = rgb.r, .g = rgb.g, .b = rgb.b, .a = 255};
  }

  /// raylib colour of an edge kind.
  ///
  /// \param kind Edge kind.
  /// \return Translucent colour.
  Color
  edge_color(EdgeKind kind)
  {
    switch (kind)
    {
    case EdgeKind::Includes: return Fade({.r = 150, .g = 160, .b = 190, .a = 255}, kEdgeAlpha);
    case EdgeKind::Calls: return Fade({.r = 240, .g = 196, .b = 96, .a = 255}, kEdgeAlpha);
    case EdgeKind::Inherits: return Fade({.r = 232, .g = 120, .b = 120, .a = 255}, kEdgeAlpha);
    case EdgeKind::Uses: return Fade({.r = 120, .g = 200, .b = 160, .a = 255}, kEdgeAlpha);
    case EdgeKind::DataFlow: return Fade({.r = 180, .g = 140, .b = 230, .a = 255}, kEdgeAlpha);
    default: return Fade({.r = 170, .g = 170, .b = 170, .a = 255}, kEdgeAlpha);
    }
  }

  /// Screen position of a group's centre.
  ///
  /// \param view Chart view.
  /// \param camera View camera.
  /// \param group Group index.
  /// \return raylib vector in screen pixels.
  Vector2
  screen_centre(const ChartView &view, const ViewCamera &camera, std::size_t group)
  {
    const Comprendo::Vec2 point = camera.world_to_screen(view.group_centre(group));
    return {.x = point.x, .y = point.y};
  }

  /// Screen radius of a group.
  ///
  /// \param view Chart view.
  /// \param camera View camera.
  /// \param group Group index.
  /// \return Radius in pixels.
  float
  screen_radius(const ChartView &view, const ViewCamera &camera, std::size_t group)
  {
    return view.layout().groups[group].radius * camera.zoom;
  }

  /// Draw a two-pixel ring around a group.
  ///
  /// \param view Chart view.
  /// \param camera View camera.
  /// \param group Group index.
  /// \param color Ring colour.
  void
  draw_highlight(const ChartView &view, const ViewCamera &camera, std::size_t group, Color color)
  {
    const Vector2 centre = screen_centre(view, camera, group);
    const float radius = std::max(screen_radius(view, camera, group), 1.0F) + kHighlightGap;
    outline_circle(centre, radius, color);
    outline_circle(centre, radius + 1.0F, color);
  }

  /// Draw labels of groups large enough to carry one.
  ///
  /// Labels are placed greedily in drawing order, parents first, and a label
  /// overlapping one already placed is skipped, so nested containers whose rims
  /// nearly coincide do not print over each other.
  ///
  /// \param view Chart view.
  /// \param camera View camera.
  /// \param items Visible groups.
  void
  draw_labels(const ChartView &view, const ViewCamera &camera, const std::vector<DrawItem> &items)
  {
    std::vector<Rectangle> placed;
    for (const DrawItem &item : items)
    {
      const float radius = screen_radius(view, camera, item.group);
      const bool container = item.style == DrawStyle::Container;
      if (
        radius < (container ? kContainerLabelPixels : kLeafLabelPixels)
        || placed.size() >= kMaxLabels
      )
      {
        continue;
      }
      const std::string label = view.display_label(item.group);
      const int width = MeasureText(label.c_str(), kLabelSize);
      const Vector2 centre = screen_centre(view, camera, item.group);
      const int x = static_cast<int>(centre.x) - (width / 2);
      const int y = container ? static_cast<int>(centre.y - radius) + 4
                              : static_cast<int>(centre.y) - (kLabelSize / 2);
      const Rectangle bounds {
        .x = static_cast<float>(x) - kLabelPadding,
        .y = static_cast<float>(y) - kLabelPadding,
        .width = static_cast<float>(width) + (2.0F * kLabelPadding),
        .height = static_cast<float>(kLabelSize) + (2.0F * kLabelPadding),
      };
      if (
        std::ranges::any_of(
          placed, [&](const Rectangle &other) { return CheckCollisionRecs(bounds, other); }
        )
      )
      {
        continue;
      }
      placed.push_back(bounds);
      DrawText(label.c_str(), x + 1, y + 1, kLabelSize, kShadowColor);
      DrawText(label.c_str(), x, y, kLabelSize, kLabelColor);
    }
  }
} // namespace

namespace Comprendo
{
  void
  draw_graph(const ChartView &view, const ViewCamera &camera, const GraphFrame &frame)
  {
    for (const DrawItem &item : frame.items)
    {
      if (item.style == DrawStyle::Container)
      {
        const Vector2 centre = screen_centre(view, camera, item.group);
        const float radius = screen_radius(view, camera, item.group);
        const Color color = group_color(view, item.group);
        fill_circle(centre, radius, Fade(color, kContainerFill));
        outline_circle(centre, radius, Fade(color, kContainerRing));
      }
    }
    for (const VisibleEdge &edge : frame.edges)
    {
      DrawLineEx(
        screen_centre(view, camera, edge.from),
        screen_centre(view, camera, edge.to),
        kEdgeWidth,
        edge_color(edge.kind)
      );
    }
    for (const DrawItem &item : frame.items)
    {
      const Vector2 centre = screen_centre(view, camera, item.group);
      const Color color = group_color(view, item.group);
      if (item.style == DrawStyle::Disk)
      {
        fill_circle(centre, screen_radius(view, camera, item.group), color);
      }
      else if (item.style == DrawStyle::Point)
      {
        DrawRectangleV({.x = centre.x - 0.5F, .y = centre.y - 0.5F}, {.x = 1.0F, .y = 1.0F}, color);
      }
    }
    if (frame.hovered && frame.hovered != frame.selected)
    {
      draw_highlight(view, camera, *frame.hovered, kHoveredColor);
    }
    if (frame.selected)
    {
      draw_highlight(view, camera, *frame.selected, kSelectedColor);
    }
    draw_labels(view, camera, frame.items);
  }
} // namespace Comprendo
