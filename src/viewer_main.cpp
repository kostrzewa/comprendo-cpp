/// \file
/// \brief Entry point for the Comprendo raylib viewer.

#include <raylib.h>

#include <algorithm>
#include <string>
#include <string_view>

#include <fmt/format.h>

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

  /// Background color.
  constexpr Color kBackground{24, 24, 32, 255};

  /// Title font size in pixels.
  constexpr int kTitleFontSize = 40;

  /// Body font size in pixels.
  constexpr int kBodyFontSize = 20;

  /// Vertical gap between text lines.
  constexpr int kLineGap = 30;

  /// Hint shown below the build information.
  constexpr const char *kFullscreenHint = "Ctrl+F or F11: fullscreen";

  /// Number of frames rendered by the smoke test.
  constexpr int kSmokeTestFrames = 20;

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
  };

  /// Parse viewer command-line options.
  ///
  /// \param argc Number of command-line arguments.
  /// \param argv Command-line argument vector.
  /// \return Parsed options.
  Options
  parse_options(int argc, char **argv)
  {
    Options options;
    for (int i = 1; i < argc; ++i)
    {
      const std::string_view arg{argv[i]};
      if (arg == "--smoke-test")
      {
        options.smoke_test = true;
      }
      else
      {
        Comprendo::Logger::warn("Unknown argument: {}", arg);
      }
    }
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

  /// Toggle fullscreen on Ctrl+F or F11, and leave it on Escape.
  void
  handle_window_shortcuts()
  {
    const bool toggle_fullscreen =
      IsKeyPressed(KEY_F11) ||
      ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_F));
    const bool leave_fullscreen = IsKeyPressed(KEY_ESCAPE) && is_fullscreen();
    if (toggle_fullscreen || leave_fullscreen)
    {
      ToggleBorderlessWindowed();
      log_fullscreen_state("Fullscreen:");
    }
  }

  /// Draw the centered placeholder screen behind the ImGui panels.
  ///
  /// \param build_info Version and git commit string.
  void
  draw_placeholder(const std::string &build_info)
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
    const bool resized =
      (GetScreenWidth() == kSmokeResizeWidth) && (GetScreenHeight() == kSmokeResizeHeight);
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
    const bool native = (GetScreenWidth() == GetMonitorWidth(monitor)) &&
                        (GetScreenHeight() == GetMonitorHeight(monitor));
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
    case kSmokeResizeFrame:
      SetWindowSize(kSmokeResizeWidth, kSmokeResizeHeight);
      return true;
    case kSmokeStartupViewportFrame:
      Comprendo::Ui::log_viewport_state("Smoke: viewport");
      return true;
    case kSmokePanelsCheckFrame:
      return check_panels();
    case kSmokeResizeCheckFrame:
      return check_resize();
    case kSmokeFullscreenFrame:
    case kSmokeWindowedFrame:
      ToggleBorderlessWindowed();
      return true;
    case kSmokeFullscreenCheckFrame:
      return check_fullscreen();
    case kSmokeFullscreenViewportCheckFrame:
    case kSmokeWindowedViewportCheckFrame:
      return check_viewport();
    case kSmokeWindowedCheckFrame:
      return check_windowed();
    default:
      return true;
    }
  }

  /// Exercise resize, fullscreen, and ImGui paths in a hidden window.
  ///
  /// \param build_info Version and git commit string.
  /// \param ui_shell Viewer UI shell.
  /// \param state Viewer state.
  /// \return True when every check passed.
  bool
  run_smoke_test(
    const std::string &build_info, Comprendo::Ui &ui_shell, Comprendo::ViewerState &state
  )
  {
    bool passed = true;
    for (int frame = 0; frame < kSmokeTestFrames; ++frame)
    {
      passed = run_smoke_step(frame) && passed;

      BeginDrawing();
      ClearBackground(kBackground);
      draw_placeholder(build_info);
      Comprendo::Ui::begin_frame();
      ui_shell.draw(state);
      Comprendo::Ui::end_frame();
      EndDrawing();
    }
    return passed;
  }
} // namespace

/// Program entry point.
///
/// \param argc Number of command-line arguments.
/// \param argv Command-line argument vector.
/// \return Process exit code.
int
main(int argc, char **argv)
{
  Comprendo::Logger::init();
  const Options options = parse_options(argc, argv);

  unsigned int flags = FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIDDEN;

  SetConfigFlags(flags);
  InitWindow(kWindowWidth, kWindowHeight, "Comprendo");
  SetWindowMinSize(kMinWindowWidth, kMinWindowHeight);
  SetTargetFPS(kTargetFps);

  Comprendo::Ui ui_shell;
  Comprendo::ViewerState state;
  ui_shell.init(!options.smoke_test);

  // Let the window manager place the window before it becomes visible.
  if (!options.smoke_test)
  {
    ClearWindowState(FLAG_WINDOW_HIDDEN);
  }

  const std::string build_info =
    fmt::format("v{} (git {})", COMPRENDO_VERSION, COMPRENDO_GIT_COMMIT);

  bool passed = true;
  if (options.smoke_test)
  {
    passed = run_smoke_test(build_info, ui_shell, state);
  }
  else
  {
    bool first_frame = true;
    while (!WindowShouldClose() && !state.quit_requested)
    {
      BeginDrawing();
      ClearBackground(kBackground);
      draw_placeholder(build_info);

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
      ui_shell.draw(state);
      Comprendo::Ui::end_frame();

      EndDrawing();
    }
  }

  Comprendo::Ui::shutdown();
  CloseWindow();
  return passed ? 0 : 1;
}
