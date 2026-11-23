#pragma once

/// \file
/// \brief raylib drawing of a ChartView: groups, edges, highlights, and labels.

#include <raylib.h>
#include <rlgl.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include "Comprendo/ChartView.hpp"

namespace Comprendo
{
  /// What one frame of the graph shows.
  struct GraphFrame
  {
    /// Groups to draw, from ChartView::visible().
    std::vector<DrawItem> items;
    /// Edges to draw, from ChartView::shown_edges().
    std::vector<VisibleEdge> edges;
    /// Selected group, highlighted.
    std::optional<std::size_t> selected;
    /// Group under the mouse, highlighted.
    std::optional<std::size_t> hovered;
    /// Overview box to draw on top, or nothing.
    std::optional<Minimap> minimap;
    /// Graph area, whose chart extent the minimap outlines.
    ScreenRect area;
    /// Label size relative to the default, from the text size level and the font.
    float text_scale = 1.0F;
    /// TrueType file of the label font; empty for raylib's built-in font.
    std::filesystem::path label_font;
  };

  /// Draws the graph with analytic anti-aliasing.
  ///
  /// Disks, rings, and lines are drawn as single quads whose fragment shader
  /// computes coverage from the exact distance to the shape's edge in pixels,
  /// so edges are smooth at every zoom and a circle costs two triangles instead
  /// of a fan of up to 720. When the shader cannot be compiled (no GLSL 3.30),
  /// the renderer falls back to raylib's polygon shapes without anti-aliasing.
  class GraphRenderer
  {
  public:
    /// Draw the overview box: the top two hierarchy levels as dots and the
    /// part of the chart the graph area shows as a rectangle.
    ///
    /// \param view Chart view.
    /// \param camera View camera.
    /// \param minimap Overview box.
    /// \param area Graph area.
    static void
    draw_minimap(
      const ChartView &view,
      const ViewCamera &camera,
      const Minimap &minimap,
      const ScreenRect &area
    );

    /// Load the anti-aliasing shader; the raylib window must already exist.
    GraphRenderer();

    /// Unload the shader; call before the window closes.
    ~GraphRenderer();

    GraphRenderer(const GraphRenderer &) = delete;
    GraphRenderer &
    operator=(const GraphRenderer &) = delete;
    GraphRenderer(GraphRenderer &&) = delete;
    GraphRenderer &
    operator=(GraphRenderer &&) = delete;

    /// Whether the anti-aliasing shader is in use.
    ///
    /// \return False when drawing falls back to polygon shapes.
    [[nodiscard]]
    bool
    antialiased() const
    {
      return m_antialiased;
    }

    /// Draw a frame of the graph in screen space, between raylib's
    /// BeginDrawing() and EndDrawing().
    ///
    /// Draws in passes so each layer stays readable: container fills and rings,
    /// edges, leaf disks and points, highlight rings, then labels at a fixed
    /// pixel size for groups large enough to carry them (greedily placed without
    /// overlap). Lines keep their pixel width at every zoom; an edge standing
    /// for many chart edges is drawn thicker (logarithmic in its weight).
    ///
    /// \param view Chart view.
    /// \param camera View camera.
    /// \param frame Items, edges, and highlights of this frame.
    void
    draw(const ChartView &view, const ViewCamera &camera, const GraphFrame &frame);

    /// Height of graph labels in pixels for a text scale.
    ///
    /// \param text_scale Scale relative to the default label size.
    /// \return Pixel size, at least 8.
    [[nodiscard]]
    static int
    label_pixels(float text_scale);

  private:
    /// Load the label font at a pixel size, unless it is already loaded;
    /// glyphs are rasterized at exactly that size so labels stay crisp.
    ///
    /// \param file TrueType file; empty for raylib's built-in font.
    /// \param pixels Label height in pixels.
    void
    use_label_font(const std::filesystem::path &file, int pixels);

    /// Label font, when a TrueType file is in use.
    std::optional<Font> m_label_font;
    /// File of m_label_font (empty for the built-in font).
    std::filesystem::path m_label_font_file;
    /// Pixel size m_label_font was loaded at.
    int m_label_font_pixels = 0;
    /// Anti-aliasing shader.
    Shader m_shader {};
    /// Render batch whose vertex layout feeds the shader's normal attribute;
    /// raylib's default batch only binds the attributes of its default shader.
    rlRenderBatch m_batch {};
    /// True when m_shader compiled and is used.
    bool m_antialiased = false;
  };
} // namespace Comprendo
