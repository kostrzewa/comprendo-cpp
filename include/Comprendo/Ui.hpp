#pragma once

/// \file
/// \brief Dear ImGui docking shell of the viewer: menu, Explorer, Inspector, Log, status bar.

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/sinks/ringbuffer_sink.h>

#include "Comprendo/ChartView.hpp"
#include "Comprendo/Hierarchy.hpp"

namespace Comprendo
{
  /// Persistent state shared by the viewer's UI panels.
  struct ViewerState
  {
    /// Maximum length of the explorer filter text.
    static constexpr std::size_t kFilterCapacity = 128;

    /// Whether the explorer panel is visible.
    bool show_explorer = true;

    /// Whether the inspector panel is visible.
    bool show_inspector = true;

    /// Whether the log panel is visible.
    bool show_log = true;

    /// Whether the minimap is drawn in the graph area.
    bool show_minimap = true;

    /// Node and edge kinds hidden from the graph.
    ViewFilter kinds;

    /// Whether the About window is open.
    bool show_about = false;

    /// Set when the user requests to quit.
    bool quit_requested = false;

    /// Current explorer filter text.
    std::array<char, kFilterCapacity> filter {};

    /// Selected hierarchy group, shared by the graph, Explorer, and Inspector.
    std::optional<std::size_t> selected;

    /// Set when the Explorer or Inspector selects a group: the graph zooms to it.
    bool focus_requested = false;

    /// Set when the graph selects a group: the Explorer opens and scrolls to it.
    bool reveal_requested = false;

    /// Path of the loaded chart, for the status bar.
    std::string chart_path;

    /// Current zoom in screen pixels per chart unit, for the status bar.
    float zoom = 1.0F;

    /// Recent log messages shown by the Log panel; null shows a hint instead.
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> log_sink;

    /// Grouping chosen in the View menu.
    Grouping grouping = Grouping::File;

    /// Set by the View menu to fit the chart into the graph area; cleared by the
    /// viewer, which keeps the chart fitted until the user zooms or pans.
    bool fit_requested = false;

    /// Screen area of the transparent central dock node where the graph is
    /// visible; empty until the first frame has been laid out.
    ScreenRect graph_area;
  };

  /// Dear ImGui docking shell for the viewer.
  class Ui
  {
  public:
    /// Initialize rlImGui, docking, and the layout file.
    ///
    /// \param persist_layout Whether to load and save the docking layout.
    void
    init(bool persist_layout = true);

    /// Shut down rlImGui.
    static void
    shutdown();

    /// Begin an ImGui frame.
    static void
    begin_frame();

    /// Draw the dock space, menu bar, and panels.
    ///
    /// \param state Viewer state shared with the panels.
    /// \param view Loaded chart, or null when none is loaded.
    void
    draw(ViewerState &state, const ChartView *view);

    /// End the ImGui frame and render its draw data.
    static void
    end_frame();

    /// Check whether ImGui wants mouse input.
    ///
    /// \return True when ImGui wants mouse input.
    static bool
    wants_mouse();

    /// Check whether the left mouse button was double-clicked this frame.
    ///
    /// \return True on the second click of a double click.
    static bool
    double_clicked();

    /// Check whether ImGui wants keyboard input.
    ///
    /// \return True when ImGui wants keyboard input.
    static bool
    wants_keyboard();

    /// Check whether the dockable panels exist in the current ImGui context.
    ///
    /// \return True when the panels exist.
    static bool
    panels_created();

    /// Log the current raylib and ImGui viewport sizes.
    ///
    /// \param prefix Message prefix.
    static void
    log_viewport_state(const char *prefix);

    /// Check whether ImGui's display size matches the raylib window size.
    ///
    /// \return True when the sizes match.
    static bool
    viewport_matches_window();

  private:
    /// Build the default dock layout.
    ///
    /// \param dockspace_id Dock space identifier.
    static void
    build_default_layout(unsigned int dockspace_id);

    /// Draw the main menu bar.
    ///
    /// \param state Viewer state.
    void
    draw_menu_bar(ViewerState &state);

    /// Draw the Explorer: grouping toggle, filter (Ctrl+F), and the tree or the
    /// search results.
    ///
    /// \param state Viewer state.
    /// \param view Loaded chart, or null.
    void
    draw_explorer(ViewerState &state, const ChartView *view);

    /// Draw the Inspector: fields, summary, detail, children, and edges of the
    /// selection.
    ///
    /// \param state Viewer state.
    /// \param view Loaded chart, or null.
    static void
    draw_inspector(ViewerState &state, const ChartView *view);

    /// Draw the log panel.
    ///
    /// \param state Viewer state.
    static void
    draw_log(ViewerState &state);

    /// Draw the About window.
    ///
    /// \param state Viewer state.
    static void
    draw_about(ViewerState &state);

    /// Draw the status bar.
    ///
    /// \param state Viewer state.
    /// \param view Loaded chart, or null.
    static void
    draw_status_bar(const ViewerState &state, const ChartView *view);

    /// True once the default layout has been built or loaded.
    bool layout_initialized_ = false;

    /// Query of the cached search results.
    std::string search_query_;

    /// Chart of the cached search results.
    const ChartView *search_view_ = nullptr;

    /// Grouping of the cached search results.
    Grouping search_grouping_ = Grouping::File;

    /// Cached search results for the Explorer filter.
    std::vector<std::size_t> search_results_;
  };
} // namespace Comprendo
