#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>

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

    /// Whether the About window is open.
    bool show_about = false;

    /// Set when the user requests to quit.
    bool quit_requested = false;

    /// Current explorer filter text.
    std::array<char, kFilterCapacity> filter{};

    /// Label of the current selection.
    std::string selection = "(nothing selected)";
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
    void
    draw(ViewerState &state);

    /// End the ImGui frame and render its draw data.
    static void
    end_frame();

    /// Return true when ImGui wants mouse input.
    static bool
    wants_mouse();

    /// Return true when ImGui wants keyboard input.
    static bool
    wants_keyboard();

    /// Return true when the dockable panels exist in the current ImGui context.
    static bool
    panels_created();

    /// Log the current raylib and ImGui viewport sizes.
    ///
    /// \param prefix Message prefix.
    static void
    log_viewport_state(const char *prefix);

    /// Return true when ImGui's display size matches the raylib window size.
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

    /// Draw the explorer panel.
    ///
    /// \param state Viewer state.
    static void
    draw_explorer(ViewerState &state);

    /// Draw the inspector panel.
    ///
    /// \param state Viewer state.
    static void
    draw_inspector(ViewerState &state);

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
    static void
    draw_status_bar();

    /// Draw one collapsible tree group.
    ///
    /// \param label Group label.
    /// \param items Selectable item labels.
    /// \param state Viewer state updated on selection.
    static void
    draw_tree_group(
      const char *label, std::initializer_list<const char *> items, ViewerState &state
    );

    /// True once the default layout has been built or loaded.
    bool layout_initialized_ = false;
  };
} // namespace Comprendo
