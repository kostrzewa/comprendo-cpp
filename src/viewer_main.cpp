/// \file
/// \brief Entry point for the Comprendo raylib viewer.

#include <raylib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include "Comprendo/ChartStore.hpp"
#include "Comprendo/ChartView.hpp"
#include "Comprendo/ChartWatcher.hpp"
#include "Comprendo/Navigation.hpp"
#include "Comprendo/ViewerSettings.hpp"
#include "Comprendo/Cli.hpp"
#include "Comprendo/GraphRenderer.hpp"
#include "Comprendo/Ui.hpp"
#include "Logger/Logger.hpp"
#include "Profiler/Profiler.hpp"
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

  /// Length of the double-click and Backspace camera moves in seconds.
  constexpr double kJumpSeconds = 0.25;

  /// Keyboard panning speed, in graph-area sizes per second.
  constexpr float kKeyPanSpeed = 0.9F;

  /// Keyboard zoom rate: the zoom doubles (E) or halves (Q) every this many seconds.
  constexpr float kKeyZoomDoubling = 0.5F;

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
  constexpr int kSmokeTestFrames = 36;

  /// Frame on which the smoke test fits the chart and checks it is visible.
  constexpr int kSmokeChartFitFrame = 18;

  /// Frame on which the smoke test selects a group from the panels and focuses it.
  constexpr int kSmokeChartSelectFrame = 19;

  /// Frame on which the smoke test selects a symbol to open its source.
  constexpr int kSmokeSourceFrame = 27;

  /// Frame on which the smoke test checks the file was loaded without opening the panel.
  constexpr int kSmokeSourceCheckFrame = 29;

  /// Frame on which the smoke test opens the Source panel with that file.
  constexpr int kSmokeSourceOpenFrame = 31;

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

  /// Frame on which the smoke test double-click-jumps to a group.
  constexpr int kSmokeChartJumpFrame = 33;

  /// Frame on which the smoke test goes back with Backspace.
  constexpr int kSmokeChartBackFrame = 34;

  /// Window width used by the smoke test resize step.
  constexpr int kSmokeResizeWidth = 800;

  /// Window height used by the smoke test resize step.
  constexpr int kSmokeResizeHeight = 600;

  /// Frame on which the smoke test records the panels' default font size.
  constexpr int kSmokeTextBaseFrame = 3;

  /// Frame on which the smoke test enlarges the text by two levels.
  constexpr int kSmokeTextLargerFrame = 7;

  /// Frame on which the smoke test checks the enlarged font size.
  constexpr int kSmokeTextLargerCheckFrame = 8;

  /// Frame on which the smoke test restores the default text size.
  constexpr int kSmokeTextResetFrame = 9;

  /// Frame on which the smoke test checks the default font size is back.
  constexpr int kSmokeTextResetCheckFrame = 13;

  /// Frame on which the smoke test switches to the Inter font.
  constexpr int kSmokeFontFrame = 14;

  /// Frame on which the smoke test checks the panels use Inter's size.
  constexpr int kSmokeFontCheckFrame = 19;

  /// Frame on which the smoke test returns to the built-in font.
  constexpr int kSmokeFontResetFrame = 20;

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
    Comprendo::Grouping grouping = Comprendo::Grouping::Cluster;
    /// Perfetto trace written at exit; empty to not profile.
    std::string profile;
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
    app.add_option(
      "--profile", options.profile, "Write a Perfetto trace to FILE and log a profile at exit"
    );
    std::string grouping {Comprendo::to_string(Comprendo::Grouping::Cluster)};
    app.add_option("--group", grouping, "Grouping at startup: cluster, file, or namespace")
      ->check(CLI::IsMember({"cluster", "file", "namespace"}))
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

  /// Character a key types on the active keyboard layout, without modifiers.
  ///
  /// GLFW (and so raylib) names keys after their US-layout position; asking
  /// GLFW for the key's name gives the character of the user's layout instead,
  /// so a German keyboard's + key counts as +.
  ///
  /// \param key raylib key code.
  /// \return The character, or a null character when the key types none.
  char
  typed_character(int key)
  {
    const char *name = glfwGetKeyName(key, 0);
    if (name != nullptr && name[0] != '\0' && name[1] == '\0')
    {
      return name[0];
    }
    switch (key)
    {
    case KEY_KP_ADD: return '+';
    case KEY_KP_SUBTRACT: return '-';
    case KEY_KP_0: return '0';
    default:
      // raylib's codes for printable keys are their US-layout characters.
      return key >= KEY_SPACE && key <= KEY_GRAVE ? static_cast<char>(key) : '\0';
    }
  }

  /// Give the window the Comprendo icon (the blocky ¿?) in several sizes, so
  /// window managers and task bars can pick a sharp one. Without the icon file
  /// the window keeps the default icon.
  ///
  /// \param file PNG copied next to the viewer at build time.
  void
  set_window_icon(const std::filesystem::path &file)
  {
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error))
    {
      Comprendo::Logger::warn("Window icon {} not found", file.string());
      return;
    }
    Image source = LoadImage(file.c_str());
    if (!IsImageValid(source))
    {
      Comprendo::Logger::warn("Cannot load window icon {}", file.string());
      return;
    }
    ImageFormat(&source, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    constexpr std::array<int, 5> kSizes = {16, 32, 48, 128, 256};
    std::vector<Image> icons;
    for (const int size : kSizes)
    {
      Image icon = ImageCopy(source);
      ImageResize(&icon, size, size);
      icons.push_back(icon);
    }
    SetWindowIcons(icons.data(), static_cast<int>(icons.size()));
    for (const Image &icon : icons)
    {
      UnloadImage(icon);
    }
    UnloadImage(source);
  }

  /// Read the viewer settings (font, text size, panels) into the state; an
  /// invalid file is reported and the defaults are used.
  ///
  /// \param path Settings file; empty to use the defaults.
  /// \param state Viewer state receiving the font and text size.
  /// \return The settings as read, to detect later changes.
  Comprendo::ViewerSettings
  load_settings(const std::filesystem::path &path, Comprendo::ViewerState &state)
  {
    Comprendo::ViewerSettings settings;
    if (!path.empty())
    {
      if (auto loaded = Comprendo::load_viewer_settings(path))
      {
        settings = std::move(*loaded);
      }
      else
      {
        Comprendo::Logger::warn("Ignoring viewer settings: {}", loaded.error());
      }
    }
    state.font = settings.font;
    state.text_level = settings.text_level;
    state.show_explorer = settings.show_explorer;
    state.show_inspector = settings.show_inspector;
    state.show_log = settings.show_log;
    state.show_minimap = settings.show_minimap;
    state.show_source = settings.show_source;
    return settings;
  }

  /// Write the viewer settings when the font, text size, or panel visibility changed.
  ///
  /// \param path Settings file; empty to keep changes for this session only.
  /// \param state Viewer state holding the font and text size.
  /// \param saved Settings last written (or read); updated on change.
  void
  save_settings_if_changed(
    const std::filesystem::path &path,
    const Comprendo::ViewerState &state,
    Comprendo::ViewerSettings &saved
  )
  {
    const Comprendo::ViewerSettings current {
      .font = state.font,
      .text_level = state.text_level,
      .show_explorer = state.show_explorer,
      .show_inspector = state.show_inspector,
      .show_log = state.show_log,
      .show_minimap = state.show_minimap,
      .show_source = state.show_source,
    };
    if (current == saved)
    {
      return;
    }
    saved = current;
    if (path.empty())
    {
      return;
    }
    if (auto written = Comprendo::save_viewer_settings(path, current); !written)
    {
      Comprendo::Logger::warn("Cannot save viewer settings: {}", written.error());
    }
  }

  /// Change the text size on Ctrl++ (or Ctrl+=), Ctrl+-, and Ctrl+0, also with
  /// the keypad, whether or not a panel has keyboard focus.
  ///
  /// \param state Viewer state holding the text size level.
  void
  handle_text_size_keys(Comprendo::ViewerState &state)
  {
    if (!IsKeyDown(KEY_LEFT_CONTROL) && !IsKeyDown(KEY_RIGHT_CONTROL))
    {
      return;
    }
    const auto apply = [&state](int key)
    {
      switch (Comprendo::text_size_key(typed_character(key)))
      {
      case Comprendo::TextSizeKey::Larger:
        state.text_level = Comprendo::step_text_level(state.text_level, 1);
        break;
      case Comprendo::TextSizeKey::Smaller:
        state.text_level = Comprendo::step_text_level(state.text_level, -1);
        break;
      case Comprendo::TextSizeKey::Reset: state.text_level = 0; break;
      case Comprendo::TextSizeKey::None: break;
      }
    };
    for (int key = KEY_SPACE; key <= KEY_GRAVE; ++key)
    {
      if (IsKeyPressed(key))
      {
        apply(key);
      }
    }
    for (const int key : {KEY_KP_ADD, KEY_KP_SUBTRACT, KEY_KP_0})
    {
      if (IsKeyPressed(key))
      {
        apply(key);
      }
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
    /// True while the second press of a double click is held: its release
    /// keeps the jump's selection instead of picking under the moving camera.
    bool double_press = false;
    /// Running camera move (double-click centring, Backspace), if any.
    std::optional<Comprendo::CameraAnimation> animation;
    /// Views to return to with Backspace.
    Comprendo::NavigationHistory history;
    /// View when the current click sequence began, remembered by a double click.
    Comprendo::NavigationEntry click_origin;
    /// Selection at the end of the previous frame, remembered by panel jumps.
    std::optional<std::size_t> previous_selected;
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

  /// Current camera pose.
  ///
  /// \param camera Camera.
  /// \return Target and zoom.
  Comprendo::CameraPose
  pose_of(const Comprendo::ViewCamera &camera)
  {
    return {.target = camera.target, .zoom = camera.zoom};
  }

  /// Pose the camera is settling at: the end of a running move, otherwise the
  /// current pose. Jumps made during a move remember where it was heading.
  ///
  /// \param session Session holding the camera and its move.
  /// \return Target and zoom.
  Comprendo::CameraPose
  settled_pose(const GraphSession &session)
  {
    return session.animation ? session.animation->at(std::numeric_limits<double>::infinity())
                             : pose_of(session.camera);
  }

  /// Stable key of a selected group, valid across regrouping and reloads.
  ///
  /// \param view Chart view.
  /// \param group Selected group, if any.
  /// \return HierarchyGroup::key, or nothing.
  std::optional<std::uint64_t>
  group_key(const Comprendo::ChartView &view, std::optional<std::size_t> group)
  {
    return group && *group < view.hierarchy().groups.size()
      ? std::optional {view.hierarchy().groups[*group].key}
      : std::nullopt;
  }

  /// Find the group with a stable key.
  ///
  /// \param view Chart view.
  /// \param key HierarchyGroup::key.
  /// \return Group index, or nothing when the group no longer exists.
  std::optional<std::size_t>
  group_with_key(const Comprendo::ChartView &view, std::uint64_t key)
  {
    const auto &groups = view.hierarchy().groups;
    const auto found = std::ranges::find(groups, key, &Comprendo::HierarchyGroup::key);
    return found == groups.end() || found == groups.begin()
      ? std::nullopt
      : std::optional {static_cast<std::size_t>(found - groups.begin())};
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
      next.history = std::move(session->history);
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
  /// selection; then record the zoom for the status.
  ///
  /// \param session Session to update.
  /// \param state Viewer state holding the requests.
  void
  apply_requests(GraphSession &session, Comprendo::ViewerState &state)
  {
    apply_grouping(session, state);
    if (state.focus_requested && state.selected)
    {
      session.history.push(
        {.pose = settled_pose(session),
         .selected = group_key(session.view, session.previous_selected)}
      );
    }
    if (state.fit_requested || (state.focus_requested && state.selected))
    {
      session.animation.reset();
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
    session.previous_selected = state.selected;
  }

  /// Start a short camera move from the current pose.
  ///
  /// \param session Session whose camera moves.
  /// \param to Pose at the end.
  void
  animate_to(GraphSession &session, const Comprendo::CameraPose &to)
  {
    session.animation.emplace(pose_of(session.camera), to, GetTime(), kJumpSeconds);
    session.auto_fit = false;
    session.fit_group.reset();
  }

  /// Advance a running camera move.
  ///
  /// \param session Session whose camera moves.
  void
  step_animation(GraphSession &session)
  {
    if (!session.animation)
    {
      return;
    }
    const double now = GetTime();
    const Comprendo::CameraPose pose = session.animation->at(now);
    session.camera.target = pose.target;
    session.camera.zoom = pose.zoom;
    if (session.animation->finished(now))
    {
      session.animation.reset();
    }
  }

  /// Centre a group in the graph area with a short move, keeping the zoom, and
  /// remember the view before the click sequence for Backspace.
  ///
  /// \param session Session to update.
  /// \param state Viewer state receiving the selection.
  /// \param group Group to centre.
  void
  jump_to(GraphSession &session, Comprendo::ViewerState &state, std::size_t group)
  {
    session.history.push(session.click_origin);
    state.selected = group;
    state.reveal_requested = true;
    Comprendo::ViewCamera centred = session.camera;
    Comprendo::centre_on(centred, session.view.group_centre(group), graph_area(state));
    animate_to(session, {.target = centred.target, .zoom = session.camera.zoom});
  }

  /// Return to the newest remembered view and its selection.
  ///
  /// \param session Session to update.
  /// \param state Viewer state receiving the selection.
  void
  go_back(GraphSession &session, Comprendo::ViewerState &state)
  {
    const auto entry = session.history.pop();
    if (!entry)
    {
      return;
    }
    state.selected
      = entry->selected ? group_with_key(session.view, *entry->selected) : std::nullopt;
    state.reveal_requested = state.selected.has_value();
    session.previous_selected = state.selected;
    animate_to(session, entry->pose);
  }

  /// Pan with WASD and zoom with E (in) and Q (out) while the keys are held.
  ///
  /// \param session Session whose camera moves.
  /// \param state Viewer state holding the graph area.
  void
  handle_graph_keys(GraphSession &session, const Comprendo::ViewerState &state)
  {
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
    {
      return;
    }
    const Comprendo::ScreenRect area = graph_area(state);
    const float dt = GetFrameTime();
    const float step = kKeyPanSpeed * std::min(area.width, area.height) * dt;
    const Comprendo::Vec2 pan {
      .x = (IsKeyDown(KEY_A) ? step : 0.0F) - (IsKeyDown(KEY_D) ? step : 0.0F),
      .y = (IsKeyDown(KEY_W) ? step : 0.0F) - (IsKeyDown(KEY_S) ? step : 0.0F),
    };
    const float zoom_sign = (IsKeyDown(KEY_E) ? 1.0F : 0.0F) - (IsKeyDown(KEY_Q) ? 1.0F : 0.0F);
    if (pan.x == 0.0F && pan.y == 0.0F && zoom_sign == 0.0F)
    {
      return;
    }
    session.animation.reset();
    session.auto_fit = false;
    session.camera.pan(pan);
    if (zoom_sign != 0.0F)
    {
      const Comprendo::Vec2 centre {
        .x = area.x + (area.width / 2.0F), .y = area.y + (area.height / 2.0F)
      };
      session.camera.zoom_at(
        centre, std::exp2(zoom_sign * dt / kKeyZoomDoubling), kMinZoom, kMaxZoom
      );
    }
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
        jump_to(session, state, *group);
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
      session.animation.reset();
      const Vector2 delta = GetMouseDelta();
      camera.pan({.x = delta.x, .y = delta.y});
      session.auto_fit = session.auto_fit && delta.x == 0.0F && delta.y == 0.0F;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
      if (!session.dragging && !session.double_press)
      {
        state.selected = session.view.pick(camera, point, {}, state.kinds);
        state.reveal_requested = state.selected.has_value();
      }
      session.dragging = false;
      session.double_press = false;
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
    step_animation(session);
    if (!Comprendo::Ui::wants_mouse())
    {
      const Vector2 mouse = GetMousePosition();
      const Comprendo::Vec2 point {.x = mouse.x, .y = mouse.y};
      const float wheel = GetMouseWheelMove();
      if (wheel != 0.0F)
      {
        session.animation.reset();
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
        session.double_press = Comprendo::Ui::double_clicked();
        if (!session.double_press)
        {
          session.click_origin
            = {.pose = settled_pose(session), .selected = group_key(session.view, state.selected)};
        }
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
      handle_graph_keys(session, state);
    }
    if (!Comprendo::Ui::wants_text_input() && IsKeyPressed(KEY_BACKSPACE))
    {
      go_back(session, state);
    }
    apply_requests(session, state);
  }

  /// Draw the graph of a session.
  ///
  /// \param session Session to draw.
  /// \param state Viewer state holding the selection.
  /// \param renderer Graph renderer.
  /// \param label_font TrueType file of the chosen font; empty for the built-in one.
  void
  draw_session(
    const GraphSession &session,
    const Comprendo::ViewerState &state,
    Comprendo::GraphRenderer &renderer,
    const std::filesystem::path &label_font
  )
  {
    Comprendo::GraphFrame frame;
    frame.items = session.view.visible(session.camera, {}, state.kinds);
    frame.edges = session.view.shown_edges(state.kinds, state.selected);
    frame.selected = state.selected;
    frame.hovered = session.hovered;
    frame.area = graph_area(state);
    frame.label_font = label_font;
    frame.text_scale = Comprendo::text_scale(state.text_level)
      * (label_font.empty() ? 1.0F : Comprendo::font_size_factor(state.font));
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
      draw_session(*session, state, renderer, ui_shell.font_file(state));
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

  /// Find a group standing for a symbol with known lines, for the smoke test's
  /// Source panel check.
  ///
  /// \param view Chart view.
  /// \return The first such group, or nothing.
  std::optional<std::size_t>
  first_symbol_with_lines(const Comprendo::ChartView &view)
  {
    const auto &groups = view.hierarchy().groups;
    for (std::size_t group = 0; group < groups.size(); ++group)
    {
      if (groups[group].nodes.empty())
      {
        continue;
      }
      const Comprendo::Node &node = view.chart().nodes[groups[group].nodes.front()];
      if (node.kind != Comprendo::NodeKind::File && node.line_start > 0)
      {
        return group;
      }
    }
    return std::nullopt;
  }

  /// Record or check the panels' font size during the smoke test's text size
  /// steps; called right after the ImGui frame began.
  ///
  /// \param frame Current frame index.
  /// \param base_font_size Default font size, recorded on kSmokeTextBaseFrame.
  /// \return True unless a check failed.
  bool
  check_text_size(int frame, float &base_font_size)
  {
    // ImGui rounds font sizes to whole pixels.
    constexpr float kTolerance = 0.51F;
    const float size = ImGui::GetFontSize();
    switch (frame)
    {
    case kSmokeTextBaseFrame: base_font_size = size; return size > 0.0F;
    case kSmokeTextLargerCheckFrame:
      Comprendo::Logger::info("Smoke: font size {:.1f} at text level 2", size);
      return std::abs(size - (base_font_size * Comprendo::text_scale(2))) < kTolerance;
    case kSmokeTextResetCheckFrame: return size == base_font_size;
    case kSmokeFontCheckFrame:
      Comprendo::Logger::info(
        "Smoke: font {} at {:.1f} px", ImGui::GetFont()->GetDebugName(), size
      );
      return std::abs(size - (base_font_size * Comprendo::font_size_factor("inter"))) < kTolerance
        && std::string_view {ImGui::GetFont()->GetDebugName()}.starts_with("Inter");
    default: return true;
    }
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
    case kSmokeChartJumpFrame:
    {
      session.click_origin
        = {.pose = pose_of(session.camera), .selected = group_key(session.view, state.selected)};
      const float zoom = session.camera.zoom;
      jump_to(session, state, session.view.display_children(0).back());
      return session.animation && session.animation->at(1e9).zoom == zoom
        && session.history.size() > 0;
    }
    case kSmokeChartBackFrame:
    {
      const Comprendo::NavigationEntry origin = session.click_origin;
      go_back(session, state);
      return session.animation && session.animation->at(1e9) == origin.pose
        && group_key(session.view, state.selected) == origin.selected;
    }
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
    float base_font_size = 0.0F;
    for (int frame = 0; frame < kSmokeTestFrames; ++frame)
    {
      if (!run_smoke_step(frame))
      {
        Comprendo::Logger::warn("Smoke: window check failed at frame {}", frame);
        passed = false;
      }
      if (session)
      {
        if (!run_chart_step(frame, *session, state))
        {
          Comprendo::Logger::warn("Smoke: chart check failed at frame {}", frame);
          passed = false;
        }
        apply_requests(*session, state);
      }

      if (frame == kSmokeTextLargerFrame)
      {
        state.text_level = Comprendo::step_text_level(state.text_level, 2);
      }
      if (frame == kSmokeTextResetFrame)
      {
        state.text_level = 0;
      }
      if (session && frame == kSmokeSourceFrame)
      {
        state.selected = first_symbol_with_lines(session->view);
      }
      if (session && frame == kSmokeSourceOpenFrame)
      {
        state.show_source = true;
      }
      if (frame == kSmokeFontFrame)
      {
        state.font = "inter";
      }
      if (frame == kSmokeFontResetFrame)
      {
        state.font = std::string {Comprendo::kBuiltinFontId};
      }

      BeginDrawing();
      ClearBackground(kBackground);
      const auto start = std::chrono::steady_clock::now();
      ui_shell.apply_settings(state);
      Comprendo::Ui::begin_frame();
      if (!check_text_size(frame, base_font_size))
      {
        Comprendo::Logger::warn("Smoke: text size check failed at frame {}", frame);
        passed = false;
      }
      draw_frame(session, build_info, note, ui_shell, state, renderer);
      if (session && frame == kSmokeSourceCheckFrame)
      {
        Comprendo::Logger::info(
          "Smoke: source loaded in the background ({} lines)", ui_shell.source_line_count()
        );
        // The panel follows the selection in the background without opening.
        if (state.show_source || ui_shell.source_line_count() == 0)
        {
          Comprendo::Logger::warn("Smoke: source panel check failed at frame {}", frame);
          passed = false;
        }
      }
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
    const Comprendo::ProfileRecording recording {options.profile};

    unsigned int flags = FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIDDEN;

    SetConfigFlags(flags);
    InitWindow(kWindowWidth, kWindowHeight, "Comprendo");
    set_window_icon(std::filesystem::path {GetApplicationDirectory()} / "comprendo-icon.png");
    SetWindowMinSize(kMinWindowWidth, kMinWindowHeight);
    SetTargetFPS(options.smoke_test ? 0 : kTargetFps);

    Comprendo::Ui ui_shell;
    Comprendo::ViewerState state;
    ui_shell.init(!options.smoke_test);
    ui_shell.load_fonts(std::filesystem::path {GetApplicationDirectory()} / "fonts");
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
    state.log_sink->set_level(spdlog::level::info);
    spdlog::default_logger()->sinks().push_back(state.log_sink);
    state.grouping = options.grouping;
    // The smoke test runs with default settings and must not touch the user's file.
    const std::filesystem::path settings_path
      = options.smoke_test ? std::filesystem::path {} : Comprendo::viewer_settings_path();
    Comprendo::ViewerSettings saved_settings = load_settings(settings_path, state);
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
        const Comprendo::ProfileScope frame_scope {"frame"};
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

        save_settings_if_changed(settings_path, state, saved_settings);
        ui_shell.apply_settings(state);
        Comprendo::Ui::begin_frame();
        handle_text_size_keys(state);
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
          const Comprendo::ProfileScope input_scope {"input"};
          handle_graph_input(*session, state);
        }
        {
          const Comprendo::ProfileScope draw_scope {"draw"};
          draw_frame(session, build_info, note, ui_shell, state, *renderer);
        }
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
