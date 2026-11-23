#pragma once

/// \file
/// \brief Dear ImGui docking shell of the viewer: menu with status, Explorer, Inspector, Log.

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <spdlog/sinks/ringbuffer_sink.h>

#include "Comprendo/ChartView.hpp"
#include "Comprendo/Hierarchy.hpp"
#include "Comprendo/ViewerSettings.hpp"

struct ImFont;

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

    /// Whether the log panel is visible; hidden by default.
    bool show_log = false;

    /// Inspector row under the keyboard cursor, counted over all its lists.
    std::optional<std::size_t> inspector_cursor;

    /// Group the Inspector cursor belongs to; a new selection resets the cursor.
    std::optional<std::size_t> inspector_group;

    /// Rows of the Inspector's Contains tree whose default folding was flipped
    /// (see ChartView::contents_tree()); a new selection clears it.
    std::set<std::size_t> inspector_toggled;

    /// Whether the minimap is drawn in the graph area.
    bool show_minimap = true;

    /// Node and edge kinds hidden from the graph.
    ViewFilter kinds;

    /// Whether the About window is open.
    bool show_about = false;

    /// Whether the floating Fonts dialog is open.
    bool show_fonts = false;

    /// Font of the panels and graph labels (see font_choices()).
    std::string font {kBuiltinFontId};

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

    /// Path of the loaded chart, for the status.
    std::string chart_path;

    /// Current zoom in screen pixels per chart unit, for the status.
    float zoom = 1.0F;

    /// Recent log messages shown by the Log panel; null shows a hint instead.
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> log_sink;

    /// Grouping chosen in the View menu.
    Grouping grouping = Grouping::Cluster;

    /// Text size of the panels and graph labels as a level (see text_scale());
    /// 0 is the default size. Changed with Ctrl++, Ctrl+-, Ctrl+0, or the View menu.
    int text_level = 0;

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

    /// Add the TrueType fonts of font_choices() found in a directory to ImGui;
    /// fonts whose file is missing stay unavailable in the Fonts dialog.
    ///
    /// \param directory Directory holding the font files.
    void
    load_fonts(const std::filesystem::path &directory);

    /// Select the panels' font and size for the state's font and text level;
    /// takes effect with the next begin_frame().
    ///
    /// The built-in font is ImGui's crisp 13 px pixel font at the default size
    /// and its scalable vector font at other sizes. An unavailable font falls
    /// back to the built-in one.
    ///
    /// \param state Viewer state holding the font and text level.
    void
    apply_settings(const ViewerState &state) const;

    /// Whether a font can be used: the built-in font, or a loaded file.
    ///
    /// \param id Font id.
    /// \return True when the font is available.
    [[nodiscard]]
    bool
    font_available(std::string_view id) const;

    /// File of the font in use, for the graph labels.
    ///
    /// \param state Viewer state holding the font.
    /// \return The TrueType file, or empty for the built-in font.
    [[nodiscard]]
    std::filesystem::path
    font_file(const ViewerState &state) const;

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

    /// Check whether a text field (such as the Explorer filter) is being edited.
    ///
    /// Keys ImGui's navigation does not use, like Backspace for going back,
    /// reach the graph whenever this is false, even while a panel has focus.
    ///
    /// \return True while ImGui wants text input.
    static bool
    wants_text_input();

    /// Check whether the Explorer and Inspector exist in the current ImGui context
    /// (the Log is hidden by default, so it may not).
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

    /// Draw the main menu bar, with the status at its right end.
    ///
    /// \param state Viewer state.
    /// \param view Loaded chart, or null.
    void
    draw_menu_bar(ViewerState &state, const ChartView *view);

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

    /// The built-in font for the state's text size: the pixel font at the
    /// default size, the vector font otherwise.
    ///
    /// \param state Viewer state holding the text size level.
    /// \return The ImGui font.
    [[nodiscard]]
    ImFont *
    builtin_font(const ViewerState &state) const;

    /// Draw the floating Fonts dialog: pick a font (each previewed in itself)
    /// and the text size; changes apply to the whole viewer at once.
    ///
    /// \param state Viewer state.
    void
    draw_fonts(ViewerState &state) const;

    /// Draw the About window.
    ///
    /// \param state Viewer state.
    static void
    draw_about(ViewerState &state);

    /// Draw the status (chart, counts, grouping, zoom, frame rate) right-aligned
    /// in the menu bar, where docked panels cannot cover it. The most detailed
    /// variant that fits is shown; hovering shows everything, including the
    /// full chart path.
    ///
    /// \param state Viewer state.
    /// \param view Loaded chart, or null.
    static void
    draw_status(const ViewerState &state, const ChartView *view);

    /// True once the default layout has been built or loaded.
    bool layout_initialized_ = false;

    /// ImGui's pixel font, used at the default text size.
    ImFont *bitmap_font_ = nullptr;

    /// ImGui's scalable font, used at every other text size.
    ImFont *vector_font_ = nullptr;

    /// Loaded TrueType fonts by font id, with their files.
    std::map<std::string, std::pair<ImFont *, std::filesystem::path>, std::less<>> fonts_;

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
