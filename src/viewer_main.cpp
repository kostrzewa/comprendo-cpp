/// \file
/// \brief Entry point for the Comprendo raylib viewer.

#include <raylib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <cstdio>
#include <exception>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <imgui.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include "Comprendo/ChartStore.hpp"
#include "Comprendo/ChartView.hpp"
#include "Comprendo/ChartWatcher.hpp"
#include "Comprendo/Cli.hpp"
#include "Comprendo/GraphRenderer.hpp"
#include "Comprendo/Ui.hpp"
#include "Logger/Logger.hpp"
#include "Version.hpp"

namespace
{
  /// Viewer window width in pixels.
  constexpr int kWindowWidth = 1280;

  /// Viewer window height in pixels.
  constexpr int kWindowHeight = 800;

  /// Minimum window width in pixels.
  constexpr int kMinWindowWidth = 640;

  /// Minimum window height in pixels.
  constexpr int kMinWindowHeight = 480;

  /// Target frame rate.
  constexpr int kTargetFps = 60;

  /// Side of the minimap in pixels.
  constexpr float kMinimapSize = 160.0F;

  /// Distance of the minimap from the graph area's edges in pixels.
  constexpr float kMinimapMargin = 12.0F;

  /// Seconds between two checks of the chart file for changes.
  constexpr double kWatchInterval = 0.5;

  /// Seconds a change must settle before the chart is reloaded, so a burst of
  /// commits (an extraction, a busy annotation session) causes one reload.
  constexpr double kReloadDelay = 0.3;

  /// Background color.
  constexpr Color kBackground {.r = 24, .g = 24, .b = 32, .a = 255};

  /// Title font size in pixels.
  constexpr int kTitleFontSize = 40;

  /// Body font size in pixels.
  constexpr int kBodyFontSize = 20;

  /// Vertical gap between text lines.
  constexpr int kLineGap = 30;

  /// Hint shown below the build information.
  constexpr const char *kFullscreenHint = "F11: fullscreen";

  /// Zoom multiplier per mouse-wheel step.
  constexpr float kZoomStep = 1.2F;

  /// Smallest zoom, in screen pixels per chart unit.
  constexpr float kMinZoom = 1e-5F;

  /// Largest zoom, in screen pixels per chart unit.
  constexpr float kMaxZoom = 200.0F;

  /// Mouse movement in pixels after which a left press becomes a drag.
  constexpr float kDragThreshold = 4.0F;

  /// Fraction of the window left free around the chart when fitting.
  constexpr float kFitMargin = 0.08F;

  /// Fraction of the graph area left free around a group the panels focus on.
  constexpr float kFocusMargin = 0.6F;

  /// Number of frames rendered by the smoke test.
  constexpr int kSmokeTestFrames = 34;

  /// Frame on which the smoke test fits the chart and checks it is visible.
  constexpr int kSmokeChartFitFrame = 18;

  /// Frame on which the smoke test selects a group from the panels and focuses it.
  constexpr int kSmokeChartSelectFrame = 19;

  /// Frame on which the smoke test zooms into the chart.
  constexpr int kSmokeChartZoomFrame = 20;

  /// Frame on which the smoke test types into the Explorer filter.
  constexpr int kSmokeChartFilterFrame = 21;

  /// Frame on which the smoke test clears the Explorer filter.
  constexpr int kSmokeChartFilterClearFrame = 23;

  /// Frame on which the smoke test picks at the window centre.
  constexpr int kSmokeChartPickFrame = 22;

  /// Frame on which the smoke test switches to namespace grouping.
  constexpr int kSmokeChartNamespaceFrame = 24;

  /// Frame on which the smoke test checks the namespace grouping.
  constexpr int kSmokeChartNamespaceCheckFrame = 26;

  /// Frame on which the smoke test switches back to file grouping.
  constexpr int kSmokeChartFileFrame = 28;

  /// Frame on which the smoke test switches to cluster grouping.
  constexpr int kSmokeChartClusterFrame = 30;

  /// Frame on which the smoke test checks the cluster grouping is shown.
  constexpr int kSmokeChartClusterCheckFrame = 32;

  /// Window width used by the smoke test resize step.
  constexpr int kSmokeResizeWidth = 800;

  /// Window height used by the smoke test resize step.
  constexpr int kSmokeResizeHeight = 600;

  /// Frame on which the smoke test resizes the window.
  constexpr int kSmokeResizeFrame = 5;

  /// Frame on which the smoke test checks the resize.
  constexpr int kSmokeResizeCheckFrame = 6;

  /// Frame on which the smoke test checks that the ImGui panels exist.
  constexpr int kSmokePanelsCheckFrame = 2;

  /// Frame on which the smoke test logs the startup viewport.
  constexpr int kSmokeStartupViewportFrame = 1;

  /// Frame on which the smoke test enters fullscreen.
  constexpr int kSmokeFullscreenFrame = 10;

  /// Frame on which the smoke test checks fullscreen.
  constexpr int kSmokeFullscreenCheckFrame = 11;

  /// Frame on which the smoke test checks the ImGui viewport after fullscreen.
  constexpr int kSmokeFullscreenViewportCheckFrame = 12;

  /// Frame on which the smoke test leaves fullscreen.
  constexpr int kSmokeWindowedFrame = 15;

  /// Frame on which the smoke test checks windowed mode.
  constexpr int kSmokeWindowedCheckFrame = 16;

  /// Frame on which the smoke test checks the ImGui viewport after leaving fullscreen.
  constexpr int kSmokeWindowedViewportCheckFrame = 17;

  /// Command-line options for the viewer.
  struct Options
  {
    /// Render a hidden window for a fixed number of frames and exit.
    bool smoke_test = false;
    /// Chart to open.
    std::string chart {Comprendo::kDefaultChartPath};
    /// True when the chart was named on the command line.
    bool chart_given = false;
    /// Name of a symbol to select and focus at startup; empty for none.
    std::string select;
    /// Grouping shown at startup.
    Comprendo::Grouping grouping = Comprendo::Grouping::File;
  };

  /// Messages kept for the Log panel.
  constexpr std::size_t kLogPanelLines = 2000;

  /// Exit code for a malformed command line, matching `comprendo-cli`.
  constexpr int kExitUsage = 2;

  /// Parse viewer command-line options.
  ///
  /// `--help` and `--version` print to standard output and stop the program, as
  /// do malformed command lines, which print to standard error.
  ///
  /// \param argc Number of command-line arguments.
  /// \param argv Command-line argument vector.
  /// \return Parsed options, or the exit code to stop with.
  std::expected<Options, int>
  parse_options(int argc, char **argv)
  {
    CLI::App app {"Interactive viewer for Comprendo code charts.", "comprendo"};
    app.set_version_flag(
      "--version", fmt::format("comprendo {} (git {})", COMPRENDO_VERSION, COMPRENDO_GIT_COMMIT)
    );
    Options options;
    app.add_flag(
      "--smoke-test",
      options.smoke_test,
      "Render a hidden window for a fixed number of frames, exercising resize and fullscreen"
    );
    CLI::Option *chart
      = app.add_option("chart", options.chart, "Chart to open")->capture_default_str();
    app.add_option(
      "--select", options.select, "Select and focus the best match for NAME at startup"
    );
    std::string grouping {Comprendo::to_string(Comprendo::Grouping::File)};
    app.add_option("--group", grouping, "Grouping at startup: file, namespace, or cluster")
      ->check(CLI::IsMember({"file", "namespace", "cluster"}))
      ->capture_default_str();
    try
    {
      app.parse(argc, argv);
    }
    catch (const CLI::ParseError &error)
    {
      return std::unexpected(app.exit(error) == 0 ? 0 : kExitUsage);
    }
    while (Comprendo::to_string(options.grouping) != grouping)
    {
      options.grouping = Comprendo::next_grouping(options.grouping);
    }
    options.chart_given = chart->count() > 0;
    return options;
  }

  /// Return true when the window is in borderless fullscreen mode.
  bool
  is_fullscreen()
  {
    return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
  }

  /// Log the current fullscreen state and the monitor's native resolution.
  ///
  /// \param prefix Message prefix, for example "Fullscreen:".
  void
  log_fullscreen_state(const char *prefix)
  {
    const int monitor = GetCurrentMonitor();
    Comprendo::Logger::info(
      "{} {} ({}x{})",
      prefix,
      is_fullscreen() ? "on" : "off",
      GetMonitorWidth(monitor),
      GetMonitorHeight(monitor)
    );
  }

  /// Toggle fullscreen on F11, and leave it on Escape. Ctrl+F stays free for search.
  void
  handle_window_shortcuts()
  {
    const bool toggle_fullscreen = IsKeyPressed(KEY_F11);
    const bool leave_fullscreen = IsKeyPressed(KEY_ESCAPE) && is_fullscreen();
    if (toggle_fullscreen || leave_fullscreen)
    {
      ToggleBorderlessWindowed();
      log_fullscreen_state("Fullscreen:");
    }
  }

  /// Draw the centered placeholder screen shown when no chart is loaded.
  ///
  /// \param build_info Version and git commit string.
  /// \param note Why no chart is shown.
  void
  draw_placeholder(const std::string &build_info, std::string_view note)
  {
    const int screen_width = GetScreenWidth();
    const int screen_height = GetScreenHeight();
    const int title_width = MeasureText("Comprendo", kTitleFontSize);
    const int info_width = MeasureText(build_info.c_str(), kBodyFontSize);
    const int hint_width = MeasureText(kFullscreenHint, kBodyFontSize);
    const int block_width = std::max({title_width, info_width, hint_width});
    const int block_height = kTitleFontSize + (2 * kBodyFontSize) + (2 * kLineGap);
    const int block_x = (screen_width - block_width) / 2;
    const int block_y = (screen_height - block_height) / 2;

    DrawText("Comprendo", block_x, block_y, kTitleFontSize, RAYWHITE);
    DrawText(
      build_info.c_str(), block_x, block_y + kTitleFontSize + kLineGap, kBodyFontSize, LIGHTGRAY
    );
    DrawText(
      kFullscreenHint,
      block_x,
      block_y + kTitleFontSize + kBodyFontSize + (2 * kLineGap),
      kBodyFontSize,
      GRAY
    );
    const std::string text {note};
    DrawText(
      text.c_str(),
      block_x,
      block_y + kTitleFontSize + (2 * kBodyFontSize) + (3 * kLineGap),
      kBodyFontSize,
      GRAY
    );
  }

  /// A loaded chart with its camera, selection, and mouse state.
  struct GraphSession
  {
    /// Chart prepared for viewing.
    Comprendo::ChartView view;
    /// View camera.
    Comprendo::ViewCamera camera;
    /// Group under the mouse.
    std::optional<std::size_t> hovered;
    /// Mouse position when the left button went down.
    Vector2 press {};
    /// True while a left-button press is being used to pan.
    bool dragging = false;
    /// True while a left-button press that started on the minimap is held.
    bool minimap_drag = false;
    /// True until the user zooms or pans: the fit target is refitted whenever
    /// the graph area changes (startup layout, window resize, re-docked panels).
    bool auto_fit = true;
    /// Group the view is fitted to while auto-fitting; nothing for the whole chart.
    std::optional<std::size_t> fit_group;
    /// Graph area of the last fit.
    Comprendo::ScreenRect fitted_area;
  };

  /// Screen area the graph occupies: the dock's central node, or the whole
  /// window before ImGui has reported it.
  ///
  /// \param state Viewer state holding the reported area.
  /// \return Graph area.
  Comprendo::ScreenRect
  graph_area(const Comprendo::ViewerState &state)
  {
    if (state.graph_area.width > 0.0F && state.graph_area.height > 0.0F)
    {
      return state.graph_area;
    }
    return {
      .width = static_cast<float>(GetScreenWidth()),
      .height = static_cast<float>(GetScreenHeight()),
    };
  }

  /// Load a chart and prepare it for viewing.
  ///
  /// \param path Chart file.
  /// \param grouping Initial grouping.
  /// \return The session, or a message explaining why the chart is unavailable.
  std::expected<GraphSession, std::string>
  load_session(const std::string &path, Comprendo::Grouping grouping)
  {
    const auto start = std::chrono::steady_clock::now();
    auto chart = Comprendo::read_chart(path);
    if (!chart)
    {
      return std::unexpected(chart.error());
    }
    const std::size_t nodes = chart->nodes.size();
    const std::size_t edges = chart->edges.size();
    GraphSession session {.view = Comprendo::ChartView {std::move(*chart), grouping}};
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start
    );
    Comprendo::Logger::info(
      "Loaded {} ({} nodes, {} edges) in {} ms", path, nodes, edges, elapsed.count()
    );
    return session;
  }

  /// Fit the whole chart into the graph area, or the window while the dock
  /// layout is not known yet.
  ///
  /// \param session Session to update.
  /// \param area Graph area from the UI; empty before the first frame.
  void
  fit_chart(GraphSession &session, const Comprendo::ScreenRect &area)
  {
    session.camera.width = static_cast<float>(GetScreenWidth());
    session.camera.height = static_cast<float>(GetScreenHeight());
    const Comprendo::ScreenRect target = area.width > 0.0F && area.height > 0.0F
      ? area
      : Comprendo::ScreenRect {
          .width = session.camera.width,
          .height = session.camera.height,
        };
    session.camera.fit(
      session.view.group_centre(0), session.view.layout().groups.front().radius, kFitMargin, target
    );
  }

  /// Id of the first node of the selected group, to find it again after a reload.
  ///
  /// \param session Current session.
  /// \param state Viewer state holding the selection.
  /// \return Node id, or nothing when nothing is selected.
  std::optional<std::uint64_t>
  selected_node_id(const GraphSession &session, const Comprendo::ViewerState &state)
  {
    if (!state.selected)
    {
      return std::nullopt;
    }
    const auto &nodes = session.view.hierarchy().groups[*state.selected].nodes;
    return nodes.empty() ? std::nullopt
                         : std::optional {session.view.chart().nodes[nodes.front()].id};
  }

  /// Reload the chart after another process changed it.
  ///
  /// Annotation-only changes are taken over in place. Structural changes build
  /// a new view in the current grouping that keeps the camera, the auto-fit
  /// state, and the selection when its node still exists. A chart that cannot
  /// be read (for example mid-rewrite) leaves the current view untouched; the
  /// next commit triggers another attempt.
  ///
  /// \param path Chart file.
  /// \param session Current session, replaced or created on success.
  /// \param state Viewer state.
  /// \param note Placeholder note, updated while no chart is shown.
  void
  reload_session(
    const std::string &path,
    std::optional<GraphSession> &session,
    Comprendo::ViewerState &state,
    std::string &note
  )
  {
    auto chart = Comprendo::read_chart(path);
    if (!chart)
    {
      Comprendo::Logger::warn("Chart changed but cannot be read yet: {}", chart.error());
      if (!session)
      {
        note = fmt::format("No chart: create one with 'comprendo-cli extract' ({})", chart.error());
      }
      return;
    }
    if (session && session->view.update_annotations(*chart))
    {
      Comprendo::Logger::info("Reloaded annotations from {}", path);
      return;
    }
    const auto start = std::chrono::steady_clock::now();
    const std::optional<std::uint64_t> selected
      = session ? selected_node_id(*session, state) : std::nullopt;
    GraphSession next {.view = Comprendo::ChartView {std::move(*chart), state.grouping}};
    if (session)
    {
      next.camera = session->camera;
      next.auto_fit = session->auto_fit;
    }
    state.selected.reset();
    if (const auto node = selected ? next.view.node_index(*selected) : std::nullopt)
    {
      state.selected = next.view.hierarchy().group_of_node[*node];
      state.reveal_requested = true;
    }
    const bool fit = !session;
    session = std::move(next);
    if (fit)
    {
      fit_chart(*session, state.graph_area);
    }
    else if (session->auto_fit)
    {
      state.fit_requested = true;
    }
    Comprendo::Logger::info(
      "Reloaded {} ({} nodes, {} edges) in {} ms",
      path,
      session->view.chart().nodes.size(),
      session->view.chart().edges.size(),
      std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
      )
        .count()
    );
  }

  /// Describe a group for the inspector and the tooltip.
  ///
  /// \param view Chart view.
  /// \param group Group index.
  /// \return Label, kind, and the first node's location.
  std::string
  describe(const Comprendo::ChartView &view, std::size_t group)
  {
    const Comprendo::HierarchyGroup &entry = view.hierarchy().groups[group];
    if (entry.nodes.empty())
    {
      return view.display_label(group);
    }
    const Comprendo::Node &node = view.chart().nodes[entry.nodes.front()];
    return fmt::format(
      "{}\n{} at {}:{}",
      view.display_label(group),
      Comprendo::to_string(node.kind),
      node.file,
      node.line_start
    );
  }

  /// Apply the grouping chosen in the menu, keeping the selection on the same node.
  ///
  /// \param session Session to update.
  /// \param state Viewer state holding the requested grouping.
  void
  apply_grouping(GraphSession &session, Comprendo::ViewerState &state)
  {
    if (state.grouping == session.view.grouping())
    {
      return;
    }
    std::optional<std::size_t> selected_node;
    if (state.selected && !session.view.hierarchy().groups[*state.selected].nodes.empty())
    {
      selected_node = session.view.hierarchy().groups[*state.selected].nodes.front();
    }
    session.view.set_grouping(state.grouping);
    session.fit_group.reset();
    state.selected.reset();
    if (selected_node)
    {
      state.selected = session.view.hierarchy().group_of_node[*selected_node];
      state.reveal_requested = true;
    }
    session.hovered.reset();
    state.fit_requested = true;
  }

  /// Select and focus the best search match for a name, as `--select` asks.
  ///
  /// \param view Loaded chart.
  /// \param name Name to look for; empty does nothing.
  /// \param state Viewer state receiving the selection.
  void
  select_at_startup(
    const Comprendo::ChartView &view, const std::string &name, Comprendo::ViewerState &state
  )
  {
    if (name.empty())
    {
      return;
    }
    const std::vector<std::size_t> found = view.search(name, 1);
    if (found.empty())
    {
      Comprendo::Logger::warn("--select: nothing matches '{}'", name);
      return;
    }
    state.selected = found.front();
    state.focus_requested = true;
    state.reveal_requested = true;
    Comprendo::Logger::info("Selected {}", view.display_label(found.front()));
  }

  /// Apply what the menu, panels, and input requested this frame: grouping
  /// switch, fitting (kept while the view is auto-fitted), and focusing on the
  /// selection; then record the zoom for the status bar.
  ///
  /// \param session Session to update.
  /// \param state Viewer state holding the requests.
  void
  apply_requests(GraphSession &session, Comprendo::ViewerState &state)
  {
    apply_grouping(session, state);
    if (state.fit_requested || (state.focus_requested && state.selected))
    {
      session.auto_fit = true;
      session.fit_group = state.fit_requested ? std::nullopt : state.selected;
      session.fitted_area = {};
    }
    state.fit_requested = false;
    state.focus_requested = false;
    if (session.auto_fit && state.graph_area.width > 0.0F && state.graph_area != session.fitted_area)
    {
      if (session.fit_group)
      {
        session.camera.fit(
          session.view.group_centre(*session.fit_group),
          session.view.layout().groups[*session.fit_group].radius,
          kFocusMargin,
          state.graph_area
        );
      }
      else
      {
        fit_chart(session, state.graph_area);
      }
      session.fitted_area = state.graph_area;
    }
    state.zoom = session.camera.zoom;
  }

  /// Handle mouse input over the graph itself: drill-down on double click,
  /// panning by dragging, selection on click, and hover.
  ///
  /// \param session Session to update.
  /// \param state Viewer state receiving selection and focus requests.
  /// \param mouse Mouse position.
  void
  handle_graph_mouse(GraphSession &session, Comprendo::ViewerState &state, Vector2 mouse)
  {
    Comprendo::ViewCamera &camera = session.camera;
    const Comprendo::Vec2 point {.x = mouse.x, .y = mouse.y};
    if (Comprendo::Ui::double_clicked())
    {
      if (const auto group = session.view.pick(camera, point, {}, state.kinds))
      {
        state.selected = group;
        state.focus_requested = true;
        state.reveal_requested = true;
      }
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !session.dragging)
    {
      session.dragging
        = std::hypot(mouse.x - session.press.x, mouse.y - session.press.y) > kDragThreshold;
    }
    if (
      session.dragging || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)
      || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)
    )
    {
      const Vector2 delta = GetMouseDelta();
      camera.pan({.x = delta.x, .y = delta.y});
      session.auto_fit = session.auto_fit && delta.x == 0.0F && delta.y == 0.0F;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
      if (!session.dragging)
      {
        state.selected = session.view.pick(camera, point, {}, state.kinds);
        state.reveal_requested = state.selected.has_value();
      }
      session.dragging = false;
    }
    session.hovered = session.view.pick(camera, point, {}, state.kinds);
  }

  /// Handle mouse and keyboard input for the graph: zoom, pan, select, fit, regroup.
  ///
  /// Input is ignored while ImGui uses the mouse or keyboard.
  ///
  /// \param session Session to update.
  /// \param state Viewer state.
  void
  handle_graph_input(GraphSession &session, Comprendo::ViewerState &state)
  {
    Comprendo::ViewCamera &camera = session.camera;
    camera.width = static_cast<float>(GetScreenWidth());
    camera.height = static_cast<float>(GetScreenHeight());
    session.hovered.reset();
    if (!Comprendo::Ui::wants_mouse())
    {
      const Vector2 mouse = GetMousePosition();
      const Comprendo::Vec2 point {.x = mouse.x, .y = mouse.y};
      const float wheel = GetMouseWheelMove();
      if (wheel != 0.0F)
      {
        camera.zoom_at(point, std::pow(kZoomStep, wheel), kMinZoom, kMaxZoom);
        session.auto_fit = false;
      }
      const Comprendo::ScreenRect area = graph_area(state);
      const std::optional<Comprendo::Minimap> minimap = state.show_minimap
        ? std::optional {Comprendo::make_minimap(session.view, area, kMinimapSize, kMinimapMargin)}
        : std::nullopt;
      if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
      {
        session.press = mouse;
        session.dragging = false;
        session.minimap_drag = minimap && minimap->contains(point);
      }
      if (session.minimap_drag)
      {
        if (minimap && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
          Comprendo::centre_on(camera, minimap->to_world(point), area);
          session.auto_fit = false;
          session.fit_group.reset();
        }
        session.minimap_drag = !IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
      }
      else
      {
        handle_graph_mouse(session, state, mouse);
      }
    }
    if (!Comprendo::Ui::wants_keyboard())
    {
      if (IsKeyPressed(KEY_HOME))
      {
        state.fit_requested = true;
      }
      if (IsKeyPressed(KEY_G))
      {
        state.grouping = Comprendo::next_grouping(state.grouping);
      }
    }
    apply_requests(session, state);
  }

  /// Draw the graph of a session.
  ///
  /// \param session Session to draw.
  /// \param state Viewer state holding the selection.
  /// \param renderer Graph renderer.
  void
  draw_session(
    const GraphSession &session,
    const Comprendo::ViewerState &state,
    Comprendo::GraphRenderer &renderer
  )
  {
    Comprendo::GraphFrame frame;
    frame.items = session.view.visible(session.camera, {}, state.kinds);
    frame.edges = session.view.visible_edges(frame.items, state.kinds);
    frame.selected = state.selected;
    frame.hovered = session.hovered;
    frame.area = graph_area(state);
    if (state.show_minimap)
    {
      frame.minimap
        = Comprendo::make_minimap(session.view, frame.area, kMinimapSize, kMinimapMargin);
    }
    renderer.draw(session.view, session.camera, frame);
  }

  /// Show a tooltip for the group under the mouse.
  ///
  /// \param session Session whose hovered group is described.
  void
  draw_tooltip(const GraphSession &session)
  {
    if (session.hovered && !Comprendo::Ui::wants_mouse())
    {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(describe(session.view, *session.hovered).c_str());
      ImGui::EndTooltip();
    }
  }

  /// Draw one frame: the graph or the placeholder, then the ImGui shell.
  ///
  /// \param session Loaded chart, if any.
  /// \param build_info Version and git commit string.
  /// \param note Placeholder note shown without a chart.
  /// \param ui_shell Viewer UI shell.
  /// \param state Viewer state.
  /// \param renderer Graph renderer.
  void
  draw_frame(
    const std::optional<GraphSession> &session,
    const std::string &build_info,
    std::string_view note,
    Comprendo::Ui &ui_shell,
    Comprendo::ViewerState &state,
    Comprendo::GraphRenderer &renderer
  )
  {
    if (session)
    {
      draw_session(*session, state, renderer);
    }
    else
    {
      draw_placeholder(build_info, note);
    }
    ui_shell.draw(state, session ? &session->view : nullptr);
    if (session)
    {
      draw_tooltip(*session);
    }
  }

  /// Return true when the raylib window and framebuffer sizes agree.
  bool
  window_and_render_match()
  {
    return (GetScreenWidth() == GetRenderWidth()) && (GetScreenHeight() == GetRenderHeight());
  }

  /// Check that the dockable panels exist.
  ///
  /// \return True when the panels are present.
  bool
  check_panels()
  {
    const bool panels_ready = Comprendo::Ui::panels_created();
    Comprendo::Logger::info("Smoke: panels {}", panels_ready ? "ready" : "missing");
    return panels_ready;
  }

  /// Check the smoke test resize step.
  ///
  /// \return True when the window resized and the framebuffer matches.
  bool
  check_resize()
  {
    const bool resized
      = (GetScreenWidth() == kSmokeResizeWidth) && (GetScreenHeight() == kSmokeResizeHeight);
    Comprendo::Ui::log_viewport_state("Smoke: viewport");
    Comprendo::Logger::info("Smoke: resized to {}x{}", GetScreenWidth(), GetScreenHeight());
    return resized && window_and_render_match();
  }

  /// Check the smoke test fullscreen step.
  ///
  /// \return True when fullscreen covers the monitor at native resolution.
  bool
  check_fullscreen()
  {
    const int monitor = GetCurrentMonitor();
    const bool native = (GetScreenWidth() == GetMonitorWidth(monitor))
      && (GetScreenHeight() == GetMonitorHeight(monitor));
    const bool fullscreen = is_fullscreen();
    Comprendo::Ui::log_viewport_state("Smoke: viewport");
    log_fullscreen_state("Smoke: fullscreen");
    return fullscreen && native && window_and_render_match();
  }

  /// Check the smoke test windowed step.
  ///
  /// \return True when the window left fullscreen.
  bool
  check_windowed()
  {
    const bool windowed = !is_fullscreen();
    log_fullscreen_state("Smoke: fullscreen");
    return windowed;
  }

  /// Check that the ImGui viewport matches the window.
  ///
  /// \return True when ImGui's display size matches the raylib window.
  bool
  check_viewport()
  {
    const bool matches = Comprendo::Ui::viewport_matches_window();
    Comprendo::Ui::log_viewport_state("Smoke: viewport");
    return matches;
  }

  /// Apply the smoke test action for one frame.
  ///
  /// \param frame Current frame index.
  /// \return True when the frame's check passed.
  bool
  run_smoke_step(int frame)
  {
    switch (frame)
    {
    case kSmokeResizeFrame: SetWindowSize(kSmokeResizeWidth, kSmokeResizeHeight); return true;
    case kSmokeStartupViewportFrame:
      Comprendo::Ui::log_viewport_state("Smoke: viewport");
      return true;
    case kSmokePanelsCheckFrame: return check_panels();
    case kSmokeResizeCheckFrame: return check_resize();
    case kSmokeFullscreenFrame:
    case kSmokeWindowedFrame: ToggleBorderlessWindowed(); return true;
    case kSmokeFullscreenCheckFrame: return check_fullscreen();
    case kSmokeFullscreenViewportCheckFrame:
    case kSmokeWindowedViewportCheckFrame: return check_viewport();
    case kSmokeWindowedCheckFrame: return check_windowed();
    default: return true;
    }
  }

  /// Apply the smoke test's chart action for one frame.
  ///
  /// \param frame Current frame index.
  /// \param session Loaded chart.
  /// \param state Viewer state.
  /// \return True when the frame's check passed.
  bool
  run_chart_step(int frame, GraphSession &session, Comprendo::ViewerState &state)
  {
    const Comprendo::Vec2 centre {
      .x = static_cast<float>(GetScreenWidth()) / 2.0F,
      .y = static_cast<float>(GetScreenHeight()) / 2.0F,
    };
    switch (frame)
    {
    case kSmokeChartFitFrame:
      fit_chart(session, state.graph_area);
      return !session.view.visible(session.camera).empty();
    case kSmokeChartZoomFrame:
      session.camera.zoom_at(centre, 8.0F, kMinZoom, kMaxZoom);
      return true;
    case kSmokeChartPickFrame:
      state.selected = session.view.pick(session.camera, centre);
      state.reveal_requested = true;
      Comprendo::Logger::info("Smoke: picked {}", state.selected ? "a group" : "nothing");
      return true;
    case kSmokeChartSelectFrame:
      state.selected = session.view.display_children(0).front();
      state.focus_requested = true;
      state.reveal_requested = true;
      return true;
    case kSmokeChartFilterFrame:
      std::ranges::copy(std::string_view {"a"}, state.filter.begin());
      return true;
    case kSmokeChartFilterClearFrame: state.filter.fill('\0'); return true;
    case kSmokeChartNamespaceFrame: state.grouping = Comprendo::Grouping::Namespace; return true;
    case kSmokeChartNamespaceCheckFrame:
      return session.view.grouping() == Comprendo::Grouping::Namespace
        && !session.view.visible(session.camera).empty();
    case kSmokeChartFileFrame: state.grouping = Comprendo::Grouping::File; return true;
    case kSmokeChartClusterFrame: state.grouping = Comprendo::Grouping::Cluster; return true;
    case kSmokeChartClusterCheckFrame:
      return session.view.grouping() == Comprendo::Grouping::Cluster
        && !session.view.visible(session.camera).empty();
    default: return true;
    }
  }

  /// Exercise resize, fullscreen, ImGui, and (with a chart) graph paths in a
  /// hidden window.
  ///
  /// \param build_info Version and git commit string.
  /// \param session Loaded chart, if any.
  /// \param note Placeholder note shown without a chart.
  /// \param ui_shell Viewer UI shell.
  /// \param state Viewer state.
  /// \param renderer Graph renderer; with a chart it must be anti-aliased.
  /// \return True when every check passed.
  bool
  run_smoke_test(
    const std::string &build_info,
    std::optional<GraphSession> &session,
    std::string_view note,
    Comprendo::Ui &ui_shell,
    Comprendo::ViewerState &state,
    Comprendo::GraphRenderer &renderer
  )
  {
    bool passed = !session || renderer.antialiased();
    Comprendo::Logger::info("Smoke: anti-aliasing {}", renderer.antialiased() ? "on" : "off");
    std::chrono::steady_clock::duration drawing {};
    for (int frame = 0; frame < kSmokeTestFrames; ++frame)
    {
      passed = run_smoke_step(frame) && passed;
      if (session)
      {
        passed = run_chart_step(frame, *session, state) && passed;
        apply_requests(*session, state);
      }

      BeginDrawing();
      ClearBackground(kBackground);
      const auto start = std::chrono::steady_clock::now();
      Comprendo::Ui::begin_frame();
      draw_frame(session, build_info, note, ui_shell, state, renderer);
      Comprendo::Ui::end_frame();
      EndDrawing();
      drawing += std::chrono::steady_clock::now() - start;
    }
    Comprendo::Logger::info(
      "Smoke: {:.2f} ms per frame drawn",
      std::chrono::duration<double, std::milli>(drawing).count() / kSmokeTestFrames
    );
    return passed;
  }
} // namespace

namespace
{
  /// Run the viewer: open the window, load the chart, and loop until closed.
  ///
  /// \param options Parsed command-line options.
  /// \return Process exit code: 1 when the smoke test failed.
  int
  run_viewer(const Options &options)
  {
    Comprendo::Logger::init();

    unsigned int flags = FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIDDEN;

    SetConfigFlags(flags);
    InitWindow(kWindowWidth, kWindowHeight, "Comprendo");
    SetWindowMinSize(kMinWindowWidth, kMinWindowHeight);
    SetTargetFPS(options.smoke_test ? 0 : kTargetFps);

    Comprendo::Ui ui_shell;
    Comprendo::ViewerState state;
    ui_shell.init(!options.smoke_test);
    std::optional<Comprendo::GraphRenderer> renderer;
    renderer.emplace();

    // Let the window manager place the window before it becomes visible.
    if (!options.smoke_test)
    {
      ClearWindowState(FLAG_WINDOW_HIDDEN);
    }

    const std::string build_info
      = fmt::format("v{} (git {})", COMPRENDO_VERSION, COMPRENDO_GIT_COMMIT);

    std::optional<GraphSession> session;
    std::string note;
    state.chart_path = options.chart;
    state.log_sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(kLogPanelLines);
    state.log_sink->set_pattern("[%T] [%l] %v");
    spdlog::default_logger()->sinks().push_back(state.log_sink);
    state.grouping = options.grouping;
    if (auto loaded = load_session(options.chart, options.grouping))
    {
      session = std::move(*loaded);
      fit_chart(*session, state.graph_area);
      select_at_startup(session->view, options.select, state);
    }
    else
    {
      Comprendo::Logger::warn("No chart: {}", loaded.error());
      note = fmt::format("No chart: create one with 'comprendo-cli extract' ({})", loaded.error());
    }

    bool passed = true;
    if (options.smoke_test)
    {
      passed = !(options.chart_given && !session);
      passed = run_smoke_test(build_info, session, note, ui_shell, state, *renderer) && passed;
    }
    else
    {
      bool first_frame = true;
      Comprendo::ChartWatcher watcher {options.chart};
      static_cast<void>(watcher.changed());
      double next_check = GetTime() + kWatchInterval;
      std::optional<double> reload_at;
      while (!WindowShouldClose() && !state.quit_requested)
      {
        if (const double now = GetTime(); now >= next_check)
        {
          next_check = now + kWatchInterval;
          if (watcher.changed())
          {
            reload_at = now + kReloadDelay;
          }
          else if (reload_at && now >= *reload_at)
          {
            reload_at.reset();
            reload_session(options.chart, session, state, note);
          }
        }
        BeginDrawing();
        ClearBackground(kBackground);

        Comprendo::Ui::begin_frame();
        if (first_frame || IsWindowResized())
        {
          Comprendo::Ui::log_viewport_state(first_frame ? "Startup" : "Resized");
          first_frame = false;
        }
        if (!Comprendo::Ui::wants_keyboard())
        {
          handle_window_shortcuts();
        }
        if (session)
        {
          handle_graph_input(*session, state);
        }
        draw_frame(session, build_info, note, ui_shell, state, *renderer);
        Comprendo::Ui::end_frame();

        EndDrawing();
      }
    }

    renderer.reset();
    Comprendo::Ui::shutdown();
    CloseWindow();
    return passed ? 0 : 1;
  }
} // namespace

/// Program entry point.
///
/// Exceptions are reported on standard error instead of terminating silently.
///
/// \param argc Number of command-line arguments.
/// \param argv Command-line argument vector.
/// \return Process exit code.
int
main(int argc, char **argv)
{
  try
  {
    const auto parsed = parse_options(argc, argv);
    if (!parsed)
    {
      return parsed.error();
    }
    return run_viewer(*parsed);
  }
  catch (const std::exception &error)
  {
    std::fputs(fmt::format("comprendo: {}\n", error.what()).c_str(), stderr);
    return 1;
  }
}
