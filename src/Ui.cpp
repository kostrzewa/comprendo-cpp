/// \file
/// \brief Dear ImGui docking shell for the viewer.

#include "Comprendo/Ui.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#include <fmt/format.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <rlImGui.h>

#include "Logger/Logger.hpp"
#include "Version.hpp"

namespace
{
  /// Persisted ImGui layout file.
  constexpr const char *kIniPath = ".comprendo/ui.ini";

  /// Directory that holds the persisted layout.
  constexpr const char *kStateDirectory = ".comprendo";

  /// Width of the About window on first open.
  constexpr float kAboutWindowWidth = 420.0F;

  /// Draw disabled text without going through ImGui's vararg API.
  ///
  /// \param text Text to draw.
  void
  text_disabled(const char *text)
  {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
  }
} // namespace

namespace Comprendo
{
  void
  Ui::init(bool persist_layout)
  {
    rlImGuiSetup(true);

    ImGuiIO &imgui_io = ImGui::GetIO();
    // Docking comes from the pinned docking branch; enable it at runtime.
    imgui_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    if (!persist_layout)
    {
      imgui_io.IniFilename = nullptr;
      return;
    }

    // Keep the docking layout next to the chart state.
    std::error_code error;
    std::filesystem::create_directories(kStateDirectory, error);
    if (error)
    {
      Logger::warn("Cannot create {}: {}", kStateDirectory, error.message());
      imgui_io.IniFilename = nullptr;
    }
    else
    {
      imgui_io.IniFilename = kIniPath;
      layout_initialized_ = std::filesystem::exists(kIniPath);
    }
  }

  void
  Ui::shutdown()
  {
    rlImGuiShutdown();
  }

  void
  Ui::begin_frame()
  {
    rlImGuiBegin();
  }

  void
  Ui::end_frame()
  {
    rlImGuiEnd();
  }

  bool
  Ui::wants_mouse()
  {
    return ImGui::GetIO().WantCaptureMouse;
  }

  bool
  Ui::wants_keyboard()
  {
    return ImGui::GetIO().WantCaptureKeyboard;
  }

  bool
  Ui::panels_created()
  {
    return ImGui::FindWindowByName("Explorer") != nullptr
      && ImGui::FindWindowByName("Inspector") != nullptr
      && ImGui::FindWindowByName("Log") != nullptr
      && ImGui::FindWindowByName("##StatusBar") != nullptr;
  }

  void
  Ui::log_viewport_state(const char *prefix)
  {
    const ImGuiIO &imgui_io = ImGui::GetIO();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    Logger::info(
      "{} screen={}x{} render={}x{} dpi={:.2f} imgui={:.0f}x{:.0f} fb_scale={:.2f} "
      "viewport={:.0f}x{:.0f}",
      prefix,
      GetScreenWidth(),
      GetScreenHeight(),
      GetRenderWidth(),
      GetRenderHeight(),
      GetWindowScaleDPI().x,
      imgui_io.DisplaySize.x,
      imgui_io.DisplaySize.y,
      imgui_io.DisplayFramebufferScale.x,
      viewport->Size.x,
      viewport->Size.y
    );
  }

  bool
  Ui::viewport_matches_window()
  {
    const ImGuiIO &imgui_io = ImGui::GetIO();
    return (static_cast<int>(imgui_io.DisplaySize.x) == GetScreenWidth())
      && (static_cast<int>(imgui_io.DisplaySize.y) == GetScreenHeight());
  }

  void
  Ui::draw(ViewerState &state)
  {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImGuiID dockspace_id = ImGui::GetID("ComprendoDockSpace");

    if (!layout_initialized_)
    {
      build_default_layout(dockspace_id);
      layout_initialized_ = true;
    }

    // The central node is transparent so the raylib graph shows through.
    ImGui::DockSpaceOverViewport(dockspace_id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
    if (const ImGuiDockNode *central = ImGui::DockBuilderGetCentralNode(dockspace_id))
    {
      state.graph_area = {
        .x = central->Pos.x - viewport->Pos.x,
        .y = central->Pos.y - viewport->Pos.y,
        .width = central->Size.x,
        .height = central->Size.y,
      };
    }
    draw_menu_bar(state);
    draw_explorer(state);
    draw_inspector(state);
    draw_log(state);
    draw_about(state);
    draw_status_bar();

    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q))
    {
      state.quit_requested = true;
    }
  }

  void
  Ui::build_default_layout(unsigned int dockspace_id)
  {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

    // Tree left, inspector right, log bottom; the remaining node is the graph.
    ImGuiID dock_main = dockspace_id;
    const ImGuiID dock_left
      = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.22F, nullptr, &dock_main);
    const ImGuiID dock_right
      = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.30F, nullptr, &dock_main);
    const ImGuiID dock_bottom
      = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.25F, nullptr, &dock_main);

    ImGui::DockBuilderDockWindow("Explorer", dock_left);
    ImGui::DockBuilderDockWindow("Inspector", dock_right);
    ImGui::DockBuilderDockWindow("Log", dock_bottom);
    ImGui::DockBuilderFinish(dockspace_id);
  }

  void
  Ui::draw_menu_bar(ViewerState &state)
  {
    if (!ImGui::BeginMainMenuBar())
    {
      return;
    }

    if (ImGui::BeginMenu("File"))
    {
      if (ImGui::MenuItem("Quit", "Ctrl+Q"))
      {
        state.quit_requested = true;
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
      ImGui::MenuItem("Explorer", nullptr, &state.show_explorer);
      ImGui::MenuItem("Inspector", nullptr, &state.show_inspector);
      ImGui::MenuItem("Log", nullptr, &state.show_log);
      ImGui::Separator();
      if (ImGui::MenuItem("Group by file", "G", state.grouping == Grouping::File))
      {
        state.grouping = Grouping::File;
      }
      if (ImGui::MenuItem("Group by namespace", "G", state.grouping == Grouping::Namespace))
      {
        state.grouping = Grouping::Namespace;
      }
      if (ImGui::MenuItem("Fit chart", "Home"))
      {
        state.fit_requested = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Reset layout"))
      {
        layout_initialized_ = false;
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
      if (ImGui::MenuItem("About"))
      {
        state.show_about = true;
      }
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }

  void
  Ui::draw_explorer(ViewerState &state)
  {
    if (!state.show_explorer)
    {
      return;
    }

    if (ImGui::Begin("Explorer", &state.show_explorer))
    {
      ImGui::InputTextWithHint(
        "##filter", "Filter symbols...", state.filter.data(), state.filter.size()
      );
      ImGui::Separator();
      text_disabled("No chart loaded; showing project skeleton.");

      if (ImGui::TreeNodeEx("Comprendo", ImGuiTreeNodeFlags_DefaultOpen))
      {
        draw_tree_group("include", {"Logger", "Profiler"}, state);
        draw_tree_group("src", {"viewer_main.cpp", "cli_main.cpp"}, state);
        ImGui::TreePop();
      }
    }
    ImGui::End();
  }

  void
  Ui::draw_tree_group(
    const char *label, std::initializer_list<const char *> items, ViewerState &state
  )
  {
    if (!ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
    {
      return;
    }

    for (const char *item : items)
    {
      if (ImGui::Selectable(item))
      {
        state.selection = item;
      }
    }
    ImGui::TreePop();
  }

  void
  Ui::draw_inspector(ViewerState &state)
  {
    if (!state.show_inspector)
    {
      return;
    }

    if (ImGui::Begin("Inspector", &state.show_inspector))
    {
      ImGui::TextUnformatted("Selection");
      ImGui::Separator();
      ImGui::PushTextWrapPos(0.0F);
      ImGui::TextUnformatted(state.selection.c_str());
      ImGui::PopTextWrapPos();
      ImGui::Spacing();
      text_disabled("Summary, details, and edges appear here once a chart is loaded.");
    }
    ImGui::End();
  }

  void
  Ui::draw_log(ViewerState &state)
  {
    if (!state.show_log)
    {
      return;
    }

    if (ImGui::Begin("Log", &state.show_log))
    {
      text_disabled("Log output will appear here.");
    }
    ImGui::End();
  }

  void
  Ui::draw_about(ViewerState &state)
  {
    if (!state.show_about)
    {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(kAboutWindowWidth, 0.0F), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("About", &state.show_about, ImGuiWindowFlags_AlwaysAutoResize))
    {
      const std::string version = fmt::format("Comprendo {}", COMPRENDO_VERSION);
      const std::string commit = fmt::format("git {}", COMPRENDO_GIT_COMMIT);
      ImGui::TextUnformatted(version.c_str());
      ImGui::TextUnformatted(commit.c_str());
      ImGui::Separator();
      ImGui::PushTextWrapPos(0.0F);
      ImGui::TextUnformatted("Code explorer and visualizer.");
      ImGui::PopTextWrapPos();
    }
    ImGui::End();
  }

  void
  Ui::draw_status_bar()
  {
    // Fixed overlay at the bottom of the main viewport.
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
      | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking
      | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    if (ImGui::Begin("##StatusBar", nullptr, flags))
    {
      const std::string status = fmt::format(
        "Comprendo {}  |  {:.1f} FPS  |  F11 fullscreen",
        COMPRENDO_VERSION,
        ImGui::GetIO().Framerate
      );
      ImGui::TextUnformatted(status.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }
} // namespace Comprendo
