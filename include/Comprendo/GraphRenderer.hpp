#pragma once

/// \file
/// \brief raylib drawing of a ChartView: groups, edges, highlights, and labels.

#include <cstddef>
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
    /// Edges to draw, from ChartView::visible_edges().
    std::vector<VisibleEdge> edges;
    /// Selected group, highlighted.
    std::optional<std::size_t> selected;
    /// Group under the mouse, highlighted.
    std::optional<std::size_t> hovered;
  };

  /// Draw a frame of the graph in screen space, between raylib's
  /// BeginDrawing() and EndDrawing().
  ///
  /// Draws in passes so each layer stays readable: container fills and rings,
  /// edges, leaf disks and points, highlight rings, then labels at a fixed pixel
  /// size for groups large enough to carry them (at most a fixed number per
  /// frame). Lines stay one pixel wide at every zoom.
  ///
  /// \param view Chart view.
  /// \param camera View camera.
  /// \param frame Items, edges, and highlights of this frame.
  void
  draw_graph(const ChartView &view, const ViewCamera &camera, const GraphFrame &frame);
} // namespace Comprendo
