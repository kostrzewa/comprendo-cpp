/// \file
/// \brief Dear ImGui docking shell for the viewer.

#include "Comprendo/Ui.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <fmt/format.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <rlImGui.h>

#include "Comprendo/Chart.hpp"
#include "Comprendo/ChartView.hpp"
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

  /// Most search results listed by the Explorer.
  constexpr std::size_t kSearchLimit = 500;

  /// Child count above which Explorer leaf rows are clipped to the visible ones.
  constexpr std::size_t kClipThreshold = 256;

  /// Most children or edges listed per Inspector section.
  constexpr std::size_t kInspectorListLimit = 300;

  /// ImGui colour of a chart colour.
  ///
  /// \param rgb Colour.
  /// \return Opaque ImGui colour.
  ImVec4
  imgui_color(Comprendo::Rgb rgb)
  {
    constexpr float kScale = 1.0F / 255.0F;
    return {
      static_cast<float>(rgb.r) * kScale,
      static_cast<float>(rgb.g) * kScale,
      static_cast<float>(rgb.b) * kScale,
      1.0F,
    };
  }

  /// Select a group from a panel and ask the graph to zoom to it.
  ///
  /// \param state Viewer state.
  /// \param group Group index.
  void
  select_from_panel(Comprendo::ViewerState &state, std::size_t group)
  {
    state.selected = group;
    state.focus_requested = true;
  }

  /// Name of a provenance value.
  ///
  /// \param provenance Provenance.
  /// \return Display name.
  const char *
  provenance_name(Comprendo::Provenance provenance)
  {
    switch (provenance)
    {
    case Comprendo::Provenance::Extractor: return "extractor";
    case Comprendo::Provenance::Model: return "model";
    default: return "unknown";
    }
  }

  /// Draw the children of one group: containers as nested tree nodes, then
  /// leaves, clipped to the visible rows when there are many.
  ///
  /// \param view Chart view.
  /// \param state Viewer state.
  /// \param group Parent group.
  /// \param on_path Groups to open because they lead to a revealed selection.
  void
  draw_explorer_children(
    const Comprendo::ChartView &view,
    Comprendo::ViewerState &state,
    std::size_t group,
    const std::vector<bool> &on_path
  );

  /// Draw one Explorer row and, when open, its children.
  ///
  /// \param view Chart view.
  /// \param state Viewer state.
  /// \param group Group of the row.
  /// \param on_path Groups to open because they lead to a revealed selection.
  void
  draw_explorer_row(
    const Comprendo::ChartView &view,
    Comprendo::ViewerState &state,
    std::size_t group,
    const std::vector<bool> &on_path
  )
  {
    const bool leaf = view.hierarchy().groups[group].children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
      | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (leaf)
    {
      flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (state.selected == group)
    {
      flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (state.reveal_requested && on_path[group])
    {
      ImGui::SetNextItemOpen(true);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_color(view.group_color(group)));
    const std::string label = fmt::format("{}##g{}", view.display_label(group), group);
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
      select_from_panel(state, group);
    }
    if (state.reveal_requested && state.selected == group)
    {
      ImGui::SetScrollHereY(0.5F);
    }
    if (open && !leaf)
    {
      draw_explorer_children(view, state, group, on_path);
      ImGui::TreePop();
    }
  }

  void
  draw_explorer_children(
    const Comprendo::ChartView &view,
    Comprendo::ViewerState &state,
    std::size_t group,
    const std::vector<bool> &on_path
  )
  {
    const std::vector<std::size_t> &children = view.display_children(group);
    const auto first_leaf = static_cast<std::size_t>(
      std::ranges::find_if(
        children, [&view](std::size_t c) { return view.hierarchy().groups[c].children.empty(); }
      )
      - children.begin()
    );
    for (std::size_t i = 0; i < first_leaf; ++i)
    {
      draw_explorer_row(view, state, children[i], on_path);
    }
    const std::size_t leaves = children.size() - first_leaf;
    if (leaves <= kClipThreshold)
    {
      for (std::size_t i = first_leaf; i < children.size(); ++i)
      {
        draw_explorer_row(view, state, children[i], on_path);
      }
      return;
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(leaves));
    if (state.reveal_requested && state.selected)
    {
      const auto found = std::ranges::find(
        children.begin() + static_cast<std::ptrdiff_t>(first_leaf), children.end(), *state.selected
      );
      if (found != children.end())
      {
        clipper.IncludeItemByIndex(
          static_cast<int>(found - children.begin() - static_cast<std::ptrdiff_t>(first_leaf))
        );
      }
    }
    while (clipper.Step())
    {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
      {
        draw_explorer_row(view, state, children[first_leaf + static_cast<std::size_t>(i)], on_path);
      }
    }
  }

  /// Draw the Explorer tree of the current grouping.
  ///
  /// \param view Chart view.
  /// \param state Viewer state.
  void
  draw_explorer_tree(const Comprendo::ChartView &view, Comprendo::ViewerState &state)
  {
    std::vector<bool> on_path(view.hierarchy().groups.size(), false);
    if (state.reveal_requested && state.selected)
    {
      for (std::size_t group = view.hierarchy().groups[*state.selected].parent; group != 0;
           group = view.hierarchy().groups[group].parent)
      {
        on_path[group] = true;
      }
    }
    draw_explorer_children(view, state, 0, on_path);
  }

  /// Draw the Explorer's search results.
  ///
  /// \param view Chart view.
  /// \param state Viewer state.
  /// \param results Matching groups, best first.
  void
  draw_search_results(
    const Comprendo::ChartView &view,
    Comprendo::ViewerState &state,
    const std::vector<std::size_t> &results
  )
  {
    const std::string count = results.size() >= kSearchLimit
      ? fmt::format("{}+ matches", kSearchLimit)
      : fmt::format("{} matches", results.size());
    text_disabled(count.c_str());
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(results.size()));
    while (clipper.Step())
    {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
      {
        const std::size_t group = results[static_cast<std::size_t>(i)];
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_color(view.group_color(group)));
        const std::string label = fmt::format("{}##s{}", view.display_label(group), group);
        if (ImGui::Selectable(label.c_str(), state.selected == group))
        {
          select_from_panel(state, group);
        }
        ImGui::PopStyleColor();
        const std::vector<std::size_t> &nodes = view.hierarchy().groups[group].nodes;
        if (!nodes.empty())
        {
          const Comprendo::Node &node = view.chart().nodes[nodes.front()];
          ImGui::SameLine();
          text_disabled(fmt::format("{}  {}", Comprendo::to_string(node.kind), node.file).c_str());
        }
      }
    }
  }

  /// Draw a label/value row of the Inspector's field table.
  ///
  /// \param label Field name.
  /// \param value Field value.
  void
  field_row(const char *label, const std::string &value)
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    text_disabled(label);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value.c_str());
  }

  /// Draw one Inspector edge list: chart edges of the selection, excluding
  /// containment, each selecting its other endpoint when clicked.
  ///
  /// \param view Chart view.
  /// \param state Viewer state.
  /// \param title Section title.
  /// \param edges Edge indices.
  /// \param outgoing True to show destinations, false to show sources.
  void
  draw_edge_list(
    const Comprendo::ChartView &view,
    Comprendo::ViewerState &state,
    const char *title,
    const std::vector<std::size_t> &edges,
    bool outgoing
  )
  {
    const std::string header = fmt::format("{} ({})##{}", title, edges.size(), title);
    if (edges.empty() || !ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
      return;
    }
    for (std::size_t i = 0; i < std::min(edges.size(), kInspectorListLimit); ++i)
    {
      const Comprendo::Edge &edge = view.chart().edges[edges[i]];
      const auto other = view.node_index(outgoing ? edge.dst : edge.src);
      if (!other)
      {
        continue;
      }
      const Comprendo::Node &node = view.chart().nodes[*other];
      const std::string label = fmt::format(
        "{}  {}##e{}{}", Comprendo::to_string(edge.kind), node.qualified_name, title, edges[i]
      );
      if (ImGui::Selectable(label.c_str()))
      {
        select_from_panel(state, view.hierarchy().group_of_node[*other]);
      }
    }
  }

  /// Draw the collapsible list of a group's children, each selectable.
  ///
  /// \param view Chart view.
  /// \param state Viewer state receiving a new selection.
  /// \param group Group whose children are listed.
  void
  draw_children(const Comprendo::ChartView &view, Comprendo::ViewerState &state, std::size_t group)
  {
    const std::vector<std::size_t> &children = view.display_children(group);
    const std::string children_header = fmt::format("Contains ({})##children", children.size());
    if (!children.empty() && ImGui::CollapsingHeader(children_header.c_str()))
    {
      for (std::size_t i = 0; i < std::min(children.size(), kInspectorListLimit); ++i)
      {
        const std::string label
          = fmt::format("{}##c{}", view.display_label(children[i]), children[i]);
        if (ImGui::Selectable(label.c_str()))
        {
          select_from_panel(state, children[i]);
        }
      }
    }
  }

  /// Draw the View → Show submenu toggling node and edge kinds.
  ///
  /// \param kinds Filter to edit.
  void
  draw_kind_menu(Comprendo::ViewFilter &kinds)
  {
    if (!ImGui::BeginMenu("Show"))
    {
      return;
    }
    text_disabled("Symbols");
    for (std::size_t kind = 0; kind < Comprendo::kNodeKindCount; ++kind)
    {
      bool shown = !kinds.hidden_nodes.test(kind);
      if (
        ImGui::MenuItem(
          std::string {Comprendo::to_string(static_cast<Comprendo::NodeKind>(kind))}.c_str(),
          nullptr,
          &shown
        )
      )
      {
        kinds.hidden_nodes.set(kind, !shown);
      }
    }
    ImGui::Separator();
    text_disabled("Relationships");
    for (std::size_t kind = 0; kind < Comprendo::kEdgeKindCount; ++kind)
    {
      if (static_cast<Comprendo::EdgeKind>(kind) == Comprendo::EdgeKind::Contains)
      {
        continue;
      }
      bool shown = !kinds.hidden_edges.test(kind);
      if (
        ImGui::MenuItem(
          std::string {Comprendo::to_string(static_cast<Comprendo::EdgeKind>(kind))}.c_str(),
          nullptr,
          &shown
        )
      )
      {
        kinds.hidden_edges.set(kind, !shown);
      }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Show all"))
    {
      kinds = {};
    }
    ImGui::EndMenu();
  }

  /// Draw the Inspector body for a selected group.
  ///
  /// \param view Chart view.
  /// \param state Viewer state.
  /// \param group Selected group.
  void
  draw_selection(const Comprendo::ChartView &view, Comprendo::ViewerState &state, std::size_t group)
  {
    const Comprendo::HierarchyGroup &entry = view.hierarchy().groups[group];
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_color(view.group_color(group)));
    ImGui::TextUnformatted(view.display_label(group).c_str());
    ImGui::PopStyleColor();
    if (const Comprendo::Cluster *cluster = view.group_cluster(group))
    {
      text_disabled("Cluster");
      ImGui::PushTextWrapPos(0.0F);
      if (cluster->summary.empty())
      {
        text_disabled("No summary.");
      }
      else
      {
        ImGui::TextUnformatted(cluster->summary.c_str());
      }
      ImGui::PopTextWrapPos();
    }
    if (entry.nodes.empty())
    {
      draw_children(view, state, group);
      return;
    }
    const Comprendo::Node &node = view.chart().nodes[entry.nodes.front()];
    if (ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingFixedFit))
    {
      field_row("Kind", std::string {Comprendo::to_string(node.kind)});
      field_row("Name", node.qualified_name);
      field_row("Location", fmt::format("{}:{}-{}", node.file, node.line_start, node.line_end));
      if (!node.signature.empty())
      {
        field_row("Signature", node.signature);
      }
      field_row("Lines", std::to_string(node.loc));
      if (node.complexity > 0)
      {
        field_row("Complexity", std::to_string(node.complexity));
      }
      if (const Comprendo::Cluster *cluster = view.node_cluster(group))
      {
        field_row("Cluster", cluster->label);
      }
      const bool from_comments
        = node.provenance == Comprendo::Provenance::Extractor && !node.summary.empty();
      field_row(
        "Source",
        from_comments ? "extractor, summary from code comments" : provenance_name(node.provenance)
      );
      ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0F);
    if (node.summary.empty())
    {
      text_disabled("No summary yet: annotate the chart with /chart in opencode.");
    }
    else
    {
      ImGui::TextUnformatted(node.summary.c_str());
    }
    ImGui::PopTextWrapPos();
    if (!node.detail.empty() && ImGui::CollapsingHeader("Detail", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::PushTextWrapPos(0.0F);
      ImGui::TextUnformatted(node.detail.c_str());
      ImGui::PopTextWrapPos();
    }

    if (entry.nodes.size() > 1)
    {
      const std::string header = fmt::format("Declared in {} files", entry.nodes.size());
      if (ImGui::CollapsingHeader(header.c_str()))
      {
        for (const std::size_t member : entry.nodes)
        {
          ImGui::Bullet();
          ImGui::TextUnformatted(view.chart().nodes[member].file.c_str());
        }
      }
    }

    draw_children(view, state, group);

    std::vector<std::size_t> outgoing;
    std::vector<std::size_t> incoming;
    for (const std::size_t member : entry.nodes)
    {
      for (const std::size_t e : view.node_edges(member).outgoing)
      {
        if (view.chart().edges[e].kind != Comprendo::EdgeKind::Contains)
        {
          outgoing.push_back(e);
        }
      }
      for (const std::size_t e : view.node_edges(member).incoming)
      {
        if (view.chart().edges[e].kind != Comprendo::EdgeKind::Contains)
        {
          incoming.push_back(e);
        }
      }
    }
    draw_edge_list(view, state, "Outgoing", outgoing, true);
    draw_edge_list(view, state, "Incoming", incoming, false);
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
  Ui::double_clicked()
  {
    return ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
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
  Ui::draw(ViewerState &state, const ChartView *view)
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
    draw_explorer(state, view);
    draw_inspector(state, view);
    draw_log(state);
    draw_about(state);
    draw_status_bar(state, view);

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
      ImGui::MenuItem("Minimap", nullptr, &state.show_minimap);
      draw_kind_menu(state.kinds);
      ImGui::Separator();
      if (ImGui::MenuItem("Group by cluster", "G", state.grouping == Grouping::Cluster))
      {
        state.grouping = Grouping::Cluster;
      }
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
  Ui::draw_explorer(ViewerState &state, const ChartView *view)
  {
    if (!state.show_explorer)
    {
      return;
    }
    if (ImGui::Begin("Explorer", &state.show_explorer))
    {
      if (view == nullptr)
      {
        text_disabled("No chart loaded.");
      }
      else
      {
        int grouping = static_cast<int>(state.grouping);
        ImGui::RadioButton("Clusters", &grouping, static_cast<int>(Grouping::Cluster));
        ImGui::SameLine();
        ImGui::RadioButton("Files", &grouping, static_cast<int>(Grouping::File));
        ImGui::SameLine();
        ImGui::RadioButton("Namespaces", &grouping, static_cast<int>(Grouping::Namespace));
        state.grouping = static_cast<Grouping>(grouping);

        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F))
        {
          ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint(
          "##filter", "Filter (Ctrl+F)", state.filter.data(), state.filter.size()
        );
        ImGui::Separator();

        const std::string query {state.filter.data()};
        if (ImGui::BeginChild("##explorer_items"))
        {
          if (query.empty())
          {
            draw_explorer_tree(*view, state);
          }
          else
          {
            if (
              query != search_query_ || view != search_view_ || view->grouping() != search_grouping_
            )
            {
              search_results_ = view->search(query, kSearchLimit);
              search_query_ = query;
              search_view_ = view;
              search_grouping_ = view->grouping();
            }
            draw_search_results(*view, state, search_results_);
          }
        }
        ImGui::EndChild();
      }
    }
    ImGui::End();
    state.reveal_requested = false;
  }

  void
  Ui::draw_inspector(ViewerState &state, const ChartView *view)
  {
    if (!state.show_inspector)
    {
      return;
    }
    if (ImGui::Begin("Inspector", &state.show_inspector))
    {
      if (view == nullptr || !state.selected)
      {
        text_disabled("Nothing selected. Click a node in the graph or the Explorer.");
      }
      else
      {
        draw_selection(*view, state, *state.selected);
      }
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
      if (!state.log_sink)
      {
        text_disabled("Logging to the console only.");
      }
      else
      {
        const std::vector<std::string> lines = state.log_sink->last_formatted();
        const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(lines.size()));
        while (clipper.Step())
        {
          for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
          {
            ImGui::TextUnformatted(lines[static_cast<std::size_t>(i)].c_str());
          }
        }
        if (at_bottom)
        {
          ImGui::SetScrollHereY(1.0F);
        }
      }
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
  Ui::draw_status_bar(const ViewerState &state, const ChartView *view)
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
      const float fps = ImGui::GetIO().Framerate;
      const std::string status = view == nullptr
        ? fmt::format(
            "Comprendo {}  |  no chart  |  {:.0f} FPS  |  F11 fullscreen", COMPRENDO_VERSION, fps
          )
        : fmt::format(
            "Comprendo {}  |  {}  |  {} nodes, {} edges  |  by {}  |  zoom {:.3g}  |  {:.0f} FPS  "
            "|  "
            "F11 fullscreen",
            COMPRENDO_VERSION,
            state.chart_path,
            view->chart().nodes.size(),
            view->chart().edges.size(),
            Comprendo::to_string(view->grouping()),
            state.zoom,
            fps
          );
      ImGui::TextUnformatted(status.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }
} // namespace Comprendo
