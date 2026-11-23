/// \file
/// \brief raylib drawing of a ChartView: groups, edges, highlights, and labels.

#include "Comprendo/GraphRenderer.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Logger/Logger.hpp"

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
    const Comprendo::Rgb rgb = view.group_color(group);
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

  /// Vertex shader of the anti-aliased shapes: passes the shape parameters in
  /// the vertex normal through to the fragment shader.
  constexpr const char *kVertexShader = R"glsl(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec3 fragShape;
out vec4 fragColor;
void main()
{
  fragTexCoord = vertexTexCoord;
  fragShape = vertexNormal;
  fragColor = vertexColor;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)glsl";

  /// Fragment shader of the anti-aliased shapes.
  ///
  /// texcoord is the pixel offset from the shape's centre (circles) or from the
  /// line's centre line (lines, in y). raylib normalizes vertex normals, so the
  /// shape is encoded as a direction whose last component restores the scale:
  /// normal = normalize(radius px, half-width px, 1) for circles and
  /// normalize(0, half-width px, -1) for lines. A half-width of 0 on a circle
  /// means a filled disk. Coverage is the signed pixel distance to the edge
  /// clamped to one pixel: exact box-filter anti-aliasing for straight and gently
  /// curved edges at every zoom.
  constexpr const char *kFragmentShader = R"glsl(#version 330
in vec2 fragTexCoord;
in vec3 fragShape;
in vec4 fragColor;
out vec4 finalColor;
void main()
{
  float scale = 1.0 / abs(fragShape.z);
  float radius = fragShape.x * scale;
  float half_width = fragShape.y * scale;
  float coverage;
  if (fragShape.z < 0.0)
  {
    coverage = clamp(half_width + 0.5 - abs(fragTexCoord.y), 0.0, 1.0);
  }
  else
  {
    float distance = length(fragTexCoord);
    if (half_width > 0.0)
    {
      coverage = clamp(half_width + 0.5 - abs(distance - radius), 0.0, 1.0);
    }
    else
    {
      coverage = clamp(radius + 0.5 - distance, 0.0, 1.0);
    }
  }
  if (coverage <= 0.0)
  {
    discard;
  }
  finalColor = vec4(fragColor.rgb, fragColor.a * coverage);
}
)glsl";

  /// Quads per draw call of the graph's render batch.
  constexpr int kBatchQuads = 8192;

  /// Pixels added around every shape quad for the anti-aliased fringe.
  constexpr float kFringe = 1.5F;

  /// Radius of a point drawn as a tiny smooth disk.
  constexpr float kPointRadius = 0.75F;

  /// Draws disks, rings, and lines, anti-aliased through the shape shader or as
  /// raylib polygons when the shader is unavailable.
  struct Painter
  {
    /// True to emit shader quads, false for polygon fallback.
    bool antialiased = false;

    /// Emit one quad around a circle with its shape parameters.
    ///
    /// \param centre Screen centre.
    /// \param radius Screen radius.
    /// \param ring_half_width Half the ring width in pixels, 0 for a filled disk.
    /// \param color Colour.
    static void
    circle_quad(Vector2 centre, float radius, float ring_half_width, Color color)
    {
      const float extent = radius + ring_half_width + kFringe;
      rlCheckRenderBatchLimit(4);
      rlBegin(RL_QUADS);
      rlColor4ub(color.r, color.g, color.b, color.a);
      rlNormal3f(radius, ring_half_width, 1.0F);
      rlTexCoord2f(-extent, -extent);
      rlVertex2f(centre.x - extent, centre.y - extent);
      rlTexCoord2f(-extent, extent);
      rlVertex2f(centre.x - extent, centre.y + extent);
      rlTexCoord2f(extent, extent);
      rlVertex2f(centre.x + extent, centre.y + extent);
      rlTexCoord2f(extent, -extent);
      rlVertex2f(centre.x + extent, centre.y - extent);
      rlEnd();
    }

    /// Draw a filled disk.
    ///
    /// \param centre Screen centre.
    /// \param radius Screen radius.
    /// \param color Fill colour.
    void
    disk(Vector2 centre, float radius, Color color) const
    {
      if (antialiased)
      {
        circle_quad(centre, radius, 0.0F, color);
      }
      else
      {
        fill_circle(centre, radius, color);
      }
    }

    /// Draw a ring centred on a circle.
    ///
    /// \param centre Screen centre.
    /// \param radius Screen radius of the ring's centre line.
    /// \param width Ring width in pixels.
    /// \param color Ring colour.
    void
    ring(Vector2 centre, float radius, float width, Color color) const
    {
      if (antialiased)
      {
        circle_quad(centre, radius, width / 2.0F, color);
      }
      else
      {
        outline_circle(centre, radius, color);
      }
    }

    /// Draw a point as a tiny disk.
    ///
    /// \param centre Screen position.
    /// \param color Colour.
    void
    point(Vector2 centre, Color color) const
    {
      if (antialiased)
      {
        circle_quad(centre, kPointRadius, 0.0F, color);
      }
      else
      {
        DrawRectangleV({.x = centre.x - 0.5F, .y = centre.y - 0.5F}, {.x = 1.0F, .y = 1.0F}, color);
      }
    }

    /// Draw a straight line.
    ///
    /// \param from Start point.
    /// \param to End point.
    /// \param width Line width in pixels.
    /// \param color Line colour.
    void
    line(Vector2 from, Vector2 to, float width, Color color) const
    {
      if (!antialiased)
      {
        DrawLineEx(from, to, width, color);
        return;
      }
      const float dx = to.x - from.x;
      const float dy = to.y - from.y;
      const float length = std::sqrt((dx * dx) + (dy * dy));
      if (length <= 0.0F)
      {
        return;
      }
      const float half = (width / 2.0F) + kFringe;
      const Vector2 normal {.x = -dy / length * half, .y = dx / length * half};
      rlCheckRenderBatchLimit(4);
      rlBegin(RL_QUADS);
      rlColor4ub(color.r, color.g, color.b, color.a);
      rlNormal3f(0.0F, width / 2.0F, -1.0F);
      rlTexCoord2f(0.0F, -half);
      rlVertex2f(from.x - normal.x, from.y - normal.y);
      rlTexCoord2f(0.0F, half);
      rlVertex2f(from.x + normal.x, from.y + normal.y);
      rlTexCoord2f(0.0F, half);
      rlVertex2f(to.x + normal.x, to.y + normal.y);
      rlTexCoord2f(0.0F, -half);
      rlVertex2f(to.x - normal.x, to.y - normal.y);
      rlEnd();
    }
  };

  /// Draw a two-pixel ring around a group.
  ///
  /// \param painter Shape painter.
  /// \param view Chart view.
  /// \param camera View camera.
  /// \param group Group index.
  /// \param color Ring colour.
  void
  draw_highlight(
    const Painter &painter,
    const ChartView &view,
    const ViewCamera &camera,
    std::size_t group,
    Color color
  )
  {
    const Vector2 centre = screen_centre(view, camera, group);
    const float radius = std::max(screen_radius(view, camera, group), 1.0F) + kHighlightGap;
    painter.ring(centre, radius + 0.5F, 2.0F, color);
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
  /// Line width for an edge weight: one pixel for a single edge, growing
  /// logarithmically with the number of edges a line stands for.
  ///
  /// \param weight Summed weight.
  /// \param max_factor Largest multiple of the base width.
  /// \return Width in pixels.
  float
  edge_width(float weight, float max_factor)
  {
    return kEdgeWidth * std::min(1.0F + (0.5F * std::log2(std::max(weight, 1.0F))), max_factor);
  }

  /// Scale a colour's opacity.
  ///
  /// \param color Colour.
  /// \param factor Opacity factor; values above 1 strengthen up to opaque.
  /// \return The colour with scaled alpha.
  Color
  scale_alpha(Color color, float factor)
  {
    color.a = static_cast<unsigned char>(std::min(255.0F, static_cast<float>(color.a) * factor));
    return color;
  }

  /// Draw the frame's edges: straight lines, and lines bundled through the
  /// selection's rim (thin fibres from its children to the rim, one trunk per
  /// peer from there). While the mouse is over a group with lines, its lines
  /// (and the trunks its fibres feed) are emphasized and all others dimmed.
  ///
  /// \param painter Shape painter.
  /// \param view Chart view.
  /// \param camera View camera.
  /// \param frame Edges and hover state.
  void
  draw_edges(
    const Painter &painter,
    const Comprendo::ChartView &view,
    const Comprendo::ViewCamera &camera,
    const Comprendo::GraphFrame &frame
  )
  {
    using Comprendo::EdgeKind;
    using Comprendo::VisibleEdge;
    constexpr float kLineMaxFactor = 4.0F;
    constexpr float kFibreMaxFactor = 2.0F;
    constexpr float kTrunkMaxFactor = 6.0F;
    constexpr float kHighlight = 2.0F;
    constexpr float kDimmed = 0.2F;

    const auto touches = [&](std::size_t group)
    {
      return frame.hovered
        && (view.contains(group, *frame.hovered) || view.contains(*frame.hovered, group));
    };
    const bool hovering = std::ranges::any_of(
      frame.edges, [&](const VisibleEdge &edge) { return touches(edge.from) || touches(edge.to); }
    );
    const auto emphasis
      = [&](bool touched) { return !hovering ? 1.0F : (touched ? kHighlight : kDimmed); };

    struct Trunk
    {
      Vector2 rim {};
      std::array<float, Comprendo::kEdgeKindCount> by_kind {};
      float weight = 0.0F;
      bool touched = false;
    };
    std::map<std::pair<std::size_t, std::size_t>, Trunk> trunks;
    for (const VisibleEdge &edge : frame.edges)
    {
      const bool touched = touches(edge.from) || touches(edge.to);
      const Color color = scale_alpha(edge_color(edge.kind), emphasis(touched));
      if (edge.via == 0)
      {
        painter.line(
          screen_centre(view, camera, edge.from),
          screen_centre(view, camera, edge.to),
          edge_width(edge.weight, kLineMaxFactor),
          color
        );
        continue;
      }
      const bool from_inside = view.contains(edge.via, edge.from);
      const std::size_t near = from_inside ? edge.from : edge.to;
      const std::size_t far = from_inside ? edge.to : edge.from;
      const Vector2 centre = screen_centre(view, camera, edge.via);
      const Vector2 target = screen_centre(view, camera, far);
      const float dx = target.x - centre.x;
      const float dy = target.y - centre.y;
      const float length = std::max(std::hypot(dx, dy), 1e-6F);
      const float radius = screen_radius(view, camera, edge.via);
      const Vector2 rim {
        .x = centre.x + (dx / length * radius), .y = centre.y + (dy / length * radius)
      };
      painter.line(
        screen_centre(view, camera, near), rim, edge_width(edge.weight, kFibreMaxFactor), color
      );
      Trunk &trunk = trunks[{edge.via, far}];
      trunk.rim = rim;
      trunk.by_kind.at(static_cast<std::size_t>(edge.kind)) += edge.weight;
      trunk.weight += edge.weight;
      trunk.touched = trunk.touched || touched;
    }
    for (const auto &[ends, trunk] : trunks)
    {
      const auto kind
        = static_cast<EdgeKind>(std::ranges::max_element(trunk.by_kind) - trunk.by_kind.begin());
      painter.line(
        trunk.rim,
        screen_centre(view, camera, ends.second),
        edge_width(trunk.weight, kTrunkMaxFactor),
        scale_alpha(edge_color(kind), emphasis(trunk.touched))
      );
    }
  }
} // namespace

namespace Comprendo
{
  GraphRenderer::GraphRenderer() :
    m_shader(LoadShaderFromMemory(kVertexShader, kFragmentShader)),
    m_antialiased(IsShaderValid(m_shader) && m_shader.id != rlGetShaderIdDefault())
  {
    if (m_antialiased)
    {
      BeginShaderMode(m_shader);
      m_batch = rlLoadRenderBatch(1, kBatchQuads);
      EndShaderMode();
      Logger::info("Graph anti-aliasing: shader");
    }
    else
    {
      Logger::warn("Graph anti-aliasing unavailable (GLSL 3.30 shader failed); drawing polygons");
    }
  }

  GraphRenderer::~GraphRenderer()
  {
    if (m_antialiased)
    {
      rlUnloadRenderBatch(m_batch);
      UnloadShader(m_shader);
    }
  }

  void
  GraphRenderer::draw(const ChartView &view, const ViewCamera &camera, const GraphFrame &frame)
  {
    const Painter painter {.antialiased = m_antialiased};
    if (m_antialiased)
    {
      rlSetRenderBatchActive(&m_batch);
      BeginShaderMode(m_shader);
      rlSetTexture(rlGetTextureIdDefault());
      rlDisableBackfaceCulling();
    }
    for (const DrawItem &item : frame.items)
    {
      if (item.style == DrawStyle::Container)
      {
        const Vector2 centre = screen_centre(view, camera, item.group);
        const float radius = screen_radius(view, camera, item.group);
        const Color color = group_color(view, item.group);
        painter.disk(centre, radius, Fade(color, kContainerFill));
        painter.ring(centre, radius, 1.0F, Fade(color, kContainerRing));
      }
    }
    draw_edges(painter, view, camera, frame);
    for (const DrawItem &item : frame.items)
    {
      const Vector2 centre = screen_centre(view, camera, item.group);
      const Color color = group_color(view, item.group);
      if (item.style == DrawStyle::Disk)
      {
        painter.disk(centre, screen_radius(view, camera, item.group), color);
      }
      else if (item.style == DrawStyle::Point)
      {
        painter.point(centre, color);
      }
    }
    if (frame.hovered && frame.hovered != frame.selected)
    {
      draw_highlight(painter, view, camera, *frame.hovered, kHoveredColor);
    }
    if (frame.selected)
    {
      draw_highlight(painter, view, camera, *frame.selected, kSelectedColor);
    }
    if (m_antialiased)
    {
      rlSetRenderBatchActive(nullptr);
      rlEnableBackfaceCulling();
      rlSetTexture(0);
      EndShaderMode();
    }
    draw_labels(view, camera, frame.items);
    if (frame.minimap)
    {
      draw_minimap(view, camera, *frame.minimap, frame.area);
    }
  }

  void
  GraphRenderer::draw_minimap(
    const ChartView &view, const ViewCamera &camera, const Minimap &minimap, const ScreenRect &area
  )
  {
    constexpr Color kBoxColor {.r = 16, .g = 16, .b = 22, .a = 220};
    constexpr Color kFrameColor {.r = 90, .g = 90, .b = 110, .a = 255};
    constexpr Color kViewColor {.r = 240, .g = 200, .b = 80, .a = 255};
    constexpr float kDotRadius = 1.5F;
    const Rectangle box {
      .x = minimap.box.x,
      .y = minimap.box.y,
      .width = minimap.box.width,
      .height = minimap.box.height
    };
    DrawRectangleRec(box, kBoxColor);
    BeginScissorMode(
      static_cast<int>(box.x),
      static_cast<int>(box.y),
      static_cast<int>(box.width),
      static_cast<int>(box.height)
    );
    const auto &groups = view.hierarchy().groups;
    for (const std::size_t top : groups.front().children)
    {
      for (const std::size_t group :
           groups[top].children.empty() ? std::vector {top} : groups[top].children)
      {
        const Vec2 point = minimap.to_screen(view.group_centre(group));
        const float radius
          = std::max(kDotRadius, view.layout().groups[group].radius * minimap.scale);
        const Rgb rgb = view.group_color(group);
        DrawCircleLinesV(
          {point.x, point.y}, radius, {.r = rgb.r, .g = rgb.g, .b = rgb.b, .a = 180}
        );
      }
    }
    const Vec2 first = minimap.to_screen(camera.screen_to_world({.x = area.x, .y = area.y}));
    const Vec2 last = minimap.to_screen(
      camera.screen_to_world({.x = area.x + area.width, .y = area.y + area.height})
    );
    constexpr float kMinViewSize = 6.0F;
    const float width = std::max(last.x - first.x, kMinViewSize);
    const float height = std::max(last.y - first.y, kMinViewSize);
    DrawRectangleLinesEx(
      {
        .x = ((first.x + last.x) / 2.0F) - (width / 2.0F),
        .y = ((first.y + last.y) / 2.0F) - (height / 2.0F),
        .width = width,
        .height = height,
      },
      1.0F,
      kViewColor
    );
    EndScissorMode();
    DrawRectangleLinesEx(box, 1.0F, kFrameColor);
  }
} // namespace Comprendo
