/// \file
/// \brief Dear ImGui docking shell for the viewer.

#include "Comprendo/Ui.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <iterator>
#include <optional>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <rlImGui.h>

#include "Comprendo/Chart.hpp"
#include "Comprendo/ChartView.hpp"
#include "Comprendo/Navigation.hpp"
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

  /// Pixel size ImGui loads the TrueType fonts at; sizes are scaled from it.
  constexpr float kBuiltinFontPixels = 13.0F;

  /// Largest file the Source panel reads, in bytes.
  constexpr std::uintmax_t kMaxSourceBytes = std::uintmax_t {8} * 1024 * 1024;

  /// How often the Source panel checks its file for changes, in seconds.
  constexpr double kSourceCheckSeconds = 1.0;

  /// Draw text turned by a quarter turn inside a strip, as on the side bars
  /// of IDEs: reading bottom to top on the left edge, top to bottom on the right.
  ///
  /// \param list Draw list.
  /// \param min Top-left corner of the strip.
  /// \param size Strip size; its height is the text's length plus padding.
  /// \param text Text.
  /// \param color Text colour.
  /// \param clockwise True to read top to bottom.
  void
  draw_rotated_text(
    ImDrawList &list, ImVec2 min, ImVec2 size, const char *text, ImU32 color, bool clockwise
  )
  {
    const ImVec2 extent = ImGui::CalcTextSize(text);
    const float padding = (size.y - extent.x) / 2.0F;
    const float centre_x = min.x + (size.x / 2.0F);
    // ImGui skips glyphs outside the clip rectangle, so lay the text out
    // unclipped at the viewport's corner, then turn it into the strip.
    const ImVec2 origin = ImGui::GetMainViewport()->Pos;
    list.PushClipRectFullScreen();
    const int first = list.VtxBuffer.Size;
    list.AddText(origin, color, text);
    list.PopClipRect();
    for (int i = first; i < list.VtxBuffer.Size; ++i)
    {
      ImVec2 &pos = list.VtxBuffer[i].pos;
      const float along = pos.x - origin.x;
      const float across = pos.y - origin.y;
      pos = clockwise
        ? ImVec2(centre_x + (extent.y / 2.0F) - across, min.y + padding + along)
        : ImVec2(centre_x - (extent.y / 2.0F) + across, min.y + size.y - padding - along);
    }
  }

  /// Read a whole file.
  ///
  /// \param path File.
  /// \return Its contents; empty when it cannot be read.
  std::string
  read_file(const std::filesystem::path &path)
  {
    std::ifstream stream {path, std::ios::binary};
    return {std::istreambuf_iterator<char> {stream}, {}};
  }

  /// Colour of a highlighted span (a dark editor palette).
  ///
  /// \param kind Span kind.
  /// \return ImGui colour.
  ImVec4
  token_color(Comprendo::TokenKind kind)
  {
    constexpr float kMax = 255.0F;
    const auto rgb = [](int r, int g, int b)
    {
      return ImVec4(
        static_cast<float>(r) / kMax,
        static_cast<float>(g) / kMax,
        static_cast<float>(b) / kMax,
        1.0F
      );
    };
    switch (kind)
    {
    case Comprendo::TokenKind::Keyword: return rgb(198, 120, 221);
    case Comprendo::TokenKind::Type: return rgb(86, 182, 194);
    case Comprendo::TokenKind::Preprocessor: return rgb(209, 154, 102);
    case Comprendo::TokenKind::String:
    case Comprendo::TokenKind::Character: return rgb(152, 195, 121);
    case Comprendo::TokenKind::Number: return rgb(229, 192, 123);
    case Comprendo::TokenKind::Comment: return rgb(127, 132, 142);
    case Comprendo::TokenKind::Text:
    default: return rgb(220, 223, 228);
    }
  }

  /// Top-left corner of a dock node in the layout's nominal arrangement: each
  /// split divided by its children's reference sizes, as if every panel were
  /// shown. Unlike the node's current position this does not change when
  /// neighbouring panels are folded, so it tells which panels shared a column.
  ///
  /// \param node Dock node.
  /// \return Position in pixels of the dock space.
  Comprendo::Vec2
  nominal_position(const ImGuiDockNode &node)
  {
    std::vector<const ImGuiDockNode *> path;
    for (const ImGuiDockNode *at = &node; at != nullptr; at = at->ParentNode)
    {
      path.push_back(at);
    }
    const ImGuiDockNode &root = *path.back();
    float x = root.Pos.x;
    float y = root.Pos.y;
    float width = root.Size.x;
    float height = root.Size.y;
    // Walk from the dock space down to the node, narrowing the rectangle.
    for (std::size_t i = path.size() - 1; i > 0; --i)
    {
      const ImGuiDockNode &parent = *path[i];
      const ImGuiDockNode &child = *path[i - 1];
      const ImGuiAxis axis = parent.SplitAxis;
      const float first = parent.ChildNodes[0]->SizeRef[axis];
      const float second = parent.ChildNodes[1]->SizeRef[axis];
      const float share = first + second > 0.0F ? first / (first + second) : 0.5F;
      const bool is_second = parent.ChildNodes[1] == &child;
      float &start = axis == ImGuiAxis_X ? x : y;
      float &length = axis == ImGuiAxis_X ? width : height;
      start += is_second ? length * share : 0.0F;
      length *= is_second ? 1.0F - share : share;
    }
    return {.x = x, .y = y};
  }

  /// Whether a panel is folded into a strip on a screen edge.
  ///
  /// \param state Viewer state holding the strips.
  /// \param window Panel name.
  /// \return True when the panel is in folded_left or folded_right.
  bool
  side_folded(const Comprendo::ViewerState &state, std::string_view window)
  {
    return std::ranges::find(state.folded_left, window) != state.folded_left.end()
      || std::ranges::find(state.folded_right, window) != state.folded_right.end();
  }

  /// Most search results listed by the Explorer.
  constexpr std::size_t kSearchLimit = 500;

  /// Child count above which Explorer leaf rows are clipped to the visible ones.
  constexpr std::size_t kClipThreshold = 256;

  /// Longest time between the clicks of a double click, as on common desktops.
  constexpr float kDoubleClickSeconds = 0.5F;

  /// Rows an Inspector list keeps when space is short.
  constexpr std::size_t kInspectorMinimumRows = 3;

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

  /// One row of an Inspector list.
  struct ListEntry
  {
    /// Text shown.
    std::string label;
    /// Group selected when the row is chosen.
    std::size_t group = 0;
    /// Indentation level in a tree section.
    std::size_t depth = 0;
    /// True when the row can be unfolded (tree sections only).
    bool expandable = false;
    /// True when the row is unfolded.
    bool expanded = false;
  };

  /// One headed, scrollable list of the Inspector.
  struct ListSection
  {
    /// Heading, without the count.
    const char *title = "";
    /// Rows.
    std::vector<ListEntry> entries;
    /// True to draw the rows as a foldable tree, coloured like the Explorer.
    bool tree = false;
  };

  /// The Contains section: a group's contents as a foldable tree.
  ///
  /// \param view Chart view.
  /// \param state Viewer state holding the folding the user changed.
  /// \param group Group whose contents are listed.
  /// \return The section, one row per visible tree row.
  ListSection
  contents_section(
    const Comprendo::ChartView &view, const Comprendo::ViewerState &state, std::size_t group
  )
  {
    ListSection section {.title = "Contains", .tree = true};
    for (const Comprendo::ContentsRow &row : view.contents_tree(group, state.inspector_toggled))
    {
      section.entries.push_back({
        .label = view.display_label(row.group),
        .group = row.group,
        .depth = row.depth,
        .expandable = row.expandable,
        .expanded = row.expanded,
      });
    }
    return section;
  }

  /// Flip the folding of a Contains row.
  ///
  /// \param state Viewer state holding the folding.
  /// \param group Group of the row.
  void
  toggle_contents_row(Comprendo::ViewerState &state, std::size_t group)
  {
    if (!state.inspector_toggled.erase(group))
    {
      state.inspector_toggled.insert(group);
    }
  }

  /// Rows for a selection's edges, each naming the other endpoint.
  ///
  /// \param view Chart view.
  /// \param edges Edge indices.
  /// \param outgoing True to list destinations, false to list sources.
  /// \return One row per edge whose other endpoint is in the chart.
  std::vector<ListEntry>
  edge_entries(
    const Comprendo::ChartView &view, const std::vector<std::size_t> &edges, bool outgoing
  )
  {
    std::vector<ListEntry> entries;
    for (const std::size_t index : edges)
    {
      const Comprendo::Edge &edge = view.chart().edges[index];
      if (const auto other = view.node_index(outgoing ? edge.dst : edge.src))
      {
        entries.push_back({
          .label = fmt::format(
            "{}  {}", Comprendo::to_string(edge.kind), view.chart().nodes[*other].qualified_name
          ),
          .group = view.hierarchy().group_of_node[*other],
        });
      }
    }
    return entries;
  }

  /// Rows for a group's aggregated connections with groups of its type.
  ///
  /// \param view Chart view.
  /// \param group Group whose connections are listed.
  /// \param outgoing True for edges leaving the group, false for edges entering it.
  /// \return One row per (other group, kind), most edges first.
  std::vector<ListEntry>
  connection_entries(const Comprendo::ChartView &view, std::size_t group, bool outgoing)
  {
    std::vector<ListEntry> entries;
    for (const Comprendo::GroupConnection &connection : view.connections(group, outgoing))
    {
      entries.push_back({
        .label = fmt::format(
          "{}  {}  ({})",
          Comprendo::to_string(connection.kind),
          view.display_label(connection.group),
          connection.edges
        ),
        .group = connection.group,
      });
    }
    return entries;
  }

  /// Check whether a group's Inspector lists aggregated connections rather
  /// than its own node's edges.
  ///
  /// \param view Chart view.
  /// \param group Group index.
  /// \return True for clusters, directories, files, namespaces, and synthetic groups.
  bool
  lists_connections(const Comprendo::ChartView &view, std::size_t group)
  {
    return view.group_type(group) != Comprendo::GroupType::Symbol;
  }

  /// Draw a heading with an optional count.
  ///
  /// \param title Heading.
  /// \param count Number of entries, shown in parentheses.
  void
  section_heading(const char *title, std::optional<std::size_t> count)
  {
    const std::string text = count ? fmt::format("{} ({})", title, *count) : std::string {title};
    ImGui::SeparatorText(text.c_str());
  }

  /// Height of a bordered child showing a number of text rows.
  ///
  /// \param rows Rows to show without scrolling.
  /// \return Height in pixels.
  float
  rows_height(std::size_t rows)
  {
    return (static_cast<float>(rows) * ImGui::GetTextLineHeightWithSpacing())
      + (2.0F * ImGui::GetStyle().WindowPadding.y);
  }

  /// Draw one row of a tree section: indented, with a fold arrow when it has
  /// contents, coloured like the Explorer. The arrow folds or unfolds the row;
  /// the label selects its group.
  ///
  /// \param view Chart view.
  /// \param entry Row.
  /// \param highlighted True when the keyboard cursor is on the row.
  /// \param state Viewer state receiving the folding or selection.
  void
  draw_tree_row(
    const Comprendo::ChartView &view,
    const ListEntry &entry,
    bool highlighted,
    Comprendo::ViewerState &state
  )
  {
    const float indent = static_cast<float>(entry.depth) * ImGui::GetStyle().IndentSpacing;
    if (indent > 0.0F)
    {
      ImGui::Indent(indent);
    }
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
      | ImGuiTreeNodeFlags_NoTreePushOnOpen
      | (entry.expandable ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf);
    if (highlighted)
    {
      flags |= ImGuiTreeNodeFlags_Selected;
    }
    ImGui::SetNextItemOpen(entry.expanded);
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_color(view.group_color(entry.group)));
    ImGui::TreeNodeEx(entry.label.c_str(), flags);
    ImGui::PopStyleColor();
    if (ImGui::IsItemToggledOpen())
    {
      toggle_contents_row(state, entry.group);
    }
    else if (ImGui::IsItemClicked())
    {
      select_from_panel(state, entry.group);
    }
    if (indent > 0.0F)
    {
      ImGui::Unindent(indent);
    }
  }

  /// Draw a headed list with its own scrollbar.
  ///
  /// \param view Chart view.
  /// \param section Heading and rows.
  /// \param height Height of the scrolling list.
  /// \param first Cursor index of the section's first row.
  /// \param cursor Keyboard cursor over all Inspector rows, if any.
  /// \param scroll_to_cursor True to scroll the cursor's row into view.
  /// \param state Viewer state receiving a click selection.
  void
  draw_list_section(
    const Comprendo::ChartView &view,
    const ListSection &section,
    float height,
    std::size_t first,
    std::optional<std::size_t> cursor,
    bool scroll_to_cursor,
    Comprendo::ViewerState &state
  )
  {
    section_heading(
      section.title,
      static_cast<std::size_t>(std::ranges::count(section.entries, 0UZ, &ListEntry::depth))
    );
    if (section.entries.empty())
    {
      text_disabled("None");
      return;
    }
    ImGui::BeginChild(section.title, ImVec2(0.0F, height), ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(section.entries.size()));
    const bool cursor_here = cursor && *cursor >= first && *cursor < first + section.entries.size();
    if (cursor_here)
    {
      clipper.IncludeItemByIndex(static_cast<int>(*cursor - first));
    }
    while (clipper.Step())
    {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
      {
        const ListEntry &entry = section.entries[static_cast<std::size_t>(row)];
        const bool highlighted = cursor_here && *cursor == first + static_cast<std::size_t>(row);
        ImGui::PushID(row);
        if (section.tree)
        {
          draw_tree_row(view, entry, highlighted, state);
        }
        else if (ImGui::Selectable(entry.label.c_str(), highlighted))
        {
          select_from_panel(state, entry.group);
        }
        ImGui::PopID();
        if (highlighted && scroll_to_cursor)
        {
          ImGui::SetScrollHereY();
        }
      }
    }
    ImGui::EndChild();
  }

  /// Move the Inspector's keyboard cursor and act on Enter.
  ///
  /// Up and Down move through the rows of all lists in order; Enter selects the
  /// row's group as a click would; Right and Left unfold and fold a tree row.
  ///
  /// \param sections Lists in drawing order.
  /// \param state Viewer state holding the cursor.
  /// \return True when the cursor moved and its row should scroll into view.
  bool
  handle_inspector_keys(const std::vector<ListSection> &sections, Comprendo::ViewerState &state)
  {
    std::vector<const ListEntry *> rows;
    for (const ListSection &section : sections)
    {
      for (const ListEntry &entry : section.entries)
      {
        rows.push_back(&entry);
      }
    }
    if (rows.empty())
    {
      state.inspector_cursor.reset();
      return false;
    }
    std::optional<std::size_t> &cursor = state.inspector_cursor;
    const int delta = (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : 0)
      - (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? 1 : 0);
    const bool moved = delta != 0 || (cursor && *cursor >= rows.size());
    if (moved)
    {
      cursor = Comprendo::move_list_cursor(cursor, rows.size(), delta);
    }
    if (cursor && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))
    {
      select_from_panel(state, rows[*cursor]->group);
    }
    if (cursor && rows[*cursor]->expandable)
    {
      const bool unfold = ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !rows[*cursor]->expanded;
      const bool fold = ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && rows[*cursor]->expanded;
      if (unfold || fold)
      {
        toggle_contents_row(state, rows[*cursor]->group);
      }
    }
    return moved;
  }

  /// Height of a section's heading, including the spacing below it.
  ///
  /// \return Height in pixels.
  float
  heading_height()
  {
    const ImGuiStyle &style = ImGui::GetStyle();
    return ImGui::GetTextLineHeight() + (2.0F * style.SeparatorTextPadding.y) + style.ItemSpacing.y;
  }

  /// Draw the Inspector's Detail and lists, sharing the remaining height.
  ///
  /// Each section gets the height it needs while that fits; long sections
  /// share what is left (see share_heights()) and keep at least a few rows.
  ///
  /// \param view Chart view.
  /// \param detail Detail text, or null for groups without one.
  /// \param sections Lists in drawing order.
  /// \param state Viewer state.
  /// \param keyboard True when the Inspector has keyboard focus.
  void
  draw_list_sections(
    const Comprendo::ChartView &view,
    const std::string *detail,
    const std::vector<ListSection> &sections,
    Comprendo::ViewerState &state,
    bool keyboard
  )
  {
    const bool scroll = keyboard && handle_inspector_keys(sections, state);
    const ImGuiStyle &style = ImGui::GetStyle();
    const float empty_section = heading_height() + ImGui::GetTextLineHeightWithSpacing();
    const float wrap
      = ImGui::GetContentRegionAvail().x - (2.0F * style.WindowPadding.x) - style.ScrollbarSize;
    float fixed = 0.0F;
    std::vector<float> needs;
    const bool has_detail = detail != nullptr && !detail->empty();
    if (detail != nullptr)
    {
      fixed += has_detail ? heading_height() + style.ItemSpacing.y : empty_section;
      if (has_detail)
      {
        needs.push_back(
          ImGui::CalcTextSize(detail->c_str(), nullptr, false, wrap).y
          + (2.0F * style.WindowPadding.y)
        );
      }
    }
    for (const ListSection &section : sections)
    {
      fixed += section.entries.empty() ? empty_section : heading_height() + style.ItemSpacing.y;
      if (!section.entries.empty())
      {
        needs.push_back(rows_height(section.entries.size()));
      }
    }
    const std::vector<float> heights = Comprendo::share_heights(
      needs, ImGui::GetContentRegionAvail().y - fixed, rows_height(kInspectorMinimumRows)
    );

    std::size_t next = 0;
    if (detail != nullptr)
    {
      section_heading("Detail", std::nullopt);
      if (has_detail)
      {
        ImGui::BeginChild("Detail", ImVec2(0.0F, heights[next++]), ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextUnformatted(detail->c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
      }
      else
      {
        text_disabled("None");
      }
    }
    std::size_t first = 0;
    for (const ListSection &section : sections)
    {
      const float height = section.entries.empty() ? 0.0F : heights[next++];
      draw_list_section(view, section, height, first, state.inspector_cursor, scroll, state);
      first += section.entries.size();
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
  /// \param keyboard True when the Inspector has keyboard focus.
  void
  draw_selection(
    const Comprendo::ChartView &view,
    Comprendo::ViewerState &state,
    std::size_t group,
    bool keyboard
  )
  {
    if (state.inspector_group != group)
    {
      state.inspector_group = group;
      state.inspector_cursor.reset();
      state.inspector_toggled.clear();
    }
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
    if (!entry.directory.empty())
    {
      text_disabled(fmt::format("Directory {}/", entry.directory).c_str());
    }
    if (entry.nodes.empty())
    {
      draw_list_sections(
        view,
        nullptr,
        {
          contents_section(view, state, group),
          ListSection {.title = "Outgoing", .entries = connection_entries(view, group, true)},
          ListSection {.title = "Incoming", .entries = connection_entries(view, group, false)},
        },
        state,
        keyboard
      );
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
    draw_list_sections(
      view,
      &node.detail,
      {
        contents_section(view, state, group),
        ListSection {
          .title = "Outgoing",
          .entries = lists_connections(view, group) ? connection_entries(view, group, true)
                                                    : edge_entries(view, outgoing, true),
        },
        ListSection {
          .title = "Incoming",
          .entries = lists_connections(view, group) ? connection_entries(view, group, false)
                                                    : edge_entries(view, incoming, false),
        },
      },
      state,
      keyboard
    );
  }
} // namespace

namespace Comprendo
{
  void
  Ui::init(bool persist_layout)
  {
    rlImGuiSetup(true);

    ImGuiIO &imgui_io = ImGui::GetIO();
    // Docked panels fold to their tab with the ▼ button (see fold_menu_handler).
    imgui_io.UserData = this;
    ImGui::GetCurrentContext()->DockNodeWindowMenuHandler = &Ui::fold_menu_handler;
    bitmap_font_ = imgui_io.Fonts->Fonts.empty() ? nullptr : imgui_io.Fonts->Fonts[0];
    vector_font_ = imgui_io.Fonts->AddFontDefaultVector();
    // Docking comes from the pinned docking branch; enable it at runtime.
    imgui_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    imgui_io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // ImGui's default of 0.30 s is shorter than desktop defaults (0.4-0.5 s),
    // so ordinary double clicks registered as two single clicks.
    imgui_io.MouseDoubleClickTime = kDoubleClickSeconds;

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
  Ui::load_fonts(const std::filesystem::path &directory)
  {
    for (const FontChoice &choice : font_choices())
    {
      if (choice.file.empty())
      {
        continue;
      }
      const std::filesystem::path file = directory / choice.file;
      std::error_code error;
      if (!std::filesystem::is_regular_file(file, error))
      {
        Logger::warn("Font {} not found at {}", choice.name, file.string());
        continue;
      }
      ImFont *font = ImGui::GetIO().Fonts->AddFontFromFileTTF(file.c_str(), kBuiltinFontPixels);
      if (font != nullptr)
      {
        fonts_.emplace(std::string {choice.id}, std::pair {font, file});
      }
    }
  }

  ImFont *
  Ui::builtin_font(const ViewerState &state) const
  {
    return state.text_level == 0 || vector_font_ == nullptr ? bitmap_font_ : vector_font_;
  }

  bool
  Ui::font_available(std::string_view id) const
  {
    return id == kBuiltinFontId || fonts_.contains(id);
  }

  std::filesystem::path
  Ui::font_file(const ViewerState &state) const
  {
    const auto found = fonts_.find(state.font);
    return found == fonts_.end() ? std::filesystem::path {} : found->second.second;
  }

  void
  Ui::apply_settings(const ViewerState &state) const
  {
    const auto found = fonts_.find(state.font);
    ImGui::GetIO().FontDefault = found == fonts_.end() ? builtin_font(state) : found->second.first;
    const float factor = found == fonts_.end() ? 1.0F : font_size_factor(state.font);
    ImGuiStyle &style = ImGui::GetStyle();
    style.FontScaleMain = text_scale(state.text_level) * factor;
    // Let a docked panel fold down to exactly its tab bar (see apply_fold_request).
    style.WindowMinSize.y = ImGui::GetFrameHeight() + 1.0F;
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
  Ui::wants_text_input()
  {
    return ImGui::GetIO().WantTextInput;
  }

  bool
  Ui::panels_created()
  {
    return ImGui::FindWindowByName("Explorer") != nullptr
      && ImGui::FindWindowByName("Inspector") != nullptr;
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

    dockspace_id_ = dockspace_id;
    apply_fold_request(state);

    // A Source panel unknown to the saved layout (layouts from before it
    // existed) gets its own area below the graph instead of floating.
    // DockBuilder edits must precede the dock space's submission; the saved
    // layout's nodes exist only after its first submission, so this retries.
    if (state.show_source && !source_docked_)
    {
      const bool known = ImGui::FindWindowSettingsByID(ImHashStr("Source")) != nullptr
        || ImGui::FindWindowByName("Source") != nullptr;
      // After the first frame a missing graph area means there is nothing to
      // split; the panel then floats rather than never appearing.
      source_docked_
        = known || dock_below_graph(dockspace_id, "Source") || ImGui::GetFrameCount() > 1;
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
    draw_menu_bar(state, view);
    draw_explorer(state, view);
    draw_inspector(state, view);
    draw_log(state);
    update_source(state, view);
    draw_source(state);
    draw_folded_strips(state);
    draw_fonts(state);
    draw_about(state);

    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q))
    {
      state.quit_requested = true;
    }
  }

  void
  Ui::fold_menu_handler(ImGuiContext *context, ImGuiDockNode *node, ImGuiTabBar * /*tab_bar*/)
  {
    if (auto *ui = static_cast<Ui *>(context->IO.UserData); ui != nullptr && node != nullptr)
    {
      ui->fold_request_ = node->ID;
    }
    ImGui::CloseCurrentPopup();
  }

  float
  Ui::folded_size(const ImGuiDockNode &node)
  {
    const ImGuiAxis axis = node.ParentNode->SplitAxis;
    // Stacked panels fold to their tab bar's height; side-by-side ones to a
    // strip as wide as a few tab bar heights. Docked windows are never smaller
    // than the style's minimum window size.
    constexpr float kStripTabs = 3.0F;
    const float tab_bar = ImGui::GetFrameHeight() + 1.0F;
    const float minimum = ImGui::GetStyle().WindowMinSize[axis];
    return std::max(minimum, axis == ImGuiAxis_Y ? tab_bar : tab_bar * kStripTabs);
  }

  void
  Ui::toggle_fold(ViewerState &state, const char *window)
  {
    // A panel folded into a strip is no longer part of its dock node's window
    // list, so it unfolds directly.
    const std::string name {window};
    if (std::erase(state.folded_left, name) + std::erase(state.folded_right, name) > 0)
    {
      return;
    }
    if (const ImGuiWindow *found = ImGui::FindWindowByName(window); found != nullptr)
    {
      fold_request_ = found->DockId;
    }
  }

  bool
  Ui::panel_folded(const ViewerState &state, const char *window)
  {
    if (side_folded(state, window))
    {
      return true;
    }
    const ImGuiWindow *found = ImGui::FindWindowByName(window);
    const ImGuiDockNode *node = found == nullptr ? nullptr : found->DockNode;
    if (node == nullptr || node->ParentNode == nullptr)
    {
      return false;
    }
    const ImGuiAxis axis = node->ParentNode->SplitAxis;
    return is_folded(node->Size[axis], folded_size(*node));
  }

  void
  Ui::apply_fold_request(ViewerState &state)
  {
    const ImGuiID id = std::exchange(fold_request_, 0U);
    ImGuiDockNode *node = id == 0 ? nullptr : ImGui::DockBuilderGetNode(id);
    if (node == nullptr || node->ParentNode == nullptr)
    {
      return;
    }
    // Panels already folded into strips unfold.
    bool unfolded = false;
    for (const ImGuiWindow *window : node->Windows)
    {
      for (std::vector<std::string> *strips : {&state.folded_left, &state.folded_right})
      {
        unfolded = std::erase(*strips, std::string {window->Name}) > 0 || unfolded;
      }
    }
    if (unfolded)
    {
      return;
    }
    // A node beside the graph folds into strips on its screen edge.
    const ImGuiDockNode *graph = ImGui::DockBuilderGetCentralNode(dockspace_id_);
    if (graph != nullptr)
    {
      constexpr float kTouch = 1.0F;
      const bool left = node->Pos.x + node->Size.x <= graph->Pos.x + kTouch;
      const bool right = node->Pos.x >= graph->Pos.x + graph->Size.x - kTouch;
      if (left || right)
      {
        std::vector<std::string> &strips = left ? state.folded_left : state.folded_right;
        for (const ImGuiWindow *window : node->Windows)
        {
          strips.emplace_back(window->Name);
        }
        return;
      }
    }
    const ImGuiDockNode *parent = node->ParentNode;
    const ImGuiAxis axis = parent->SplitAxis;
    const float folded = folded_size(*node);
    const float available = parent->Size[axis] - ImGui::GetStyle().DockingSeparatorSize;
    const float current = node->Size[axis];
    const auto remembered = fold_sizes_.find(id);
    const float size = fold_toggle_size(
      current, folded, remembered == fold_sizes_.end() ? 0.0F : remembered->second, available
    );
    if (!is_folded(current, folded))
    {
      fold_sizes_[id] = current;
    }
    node->Size[axis] = node->SizeRef[axis] = size;
    node->WantLockSizeOnce = true;
    for (ImGuiDockNode *child : parent->ChildNodes)
    {
      if (child != nullptr && child != node)
      {
        child->WantLockSizeOnce = false;
      }
    }
  }

  void
  Ui::draw_folded_strips(ViewerState &state) const
  {
    const ImGuiDockNode *graph = ImGui::DockBuilderGetCentralNode(dockspace_id_);
    if (graph == nullptr)
    {
      return;
    }
    const ImGuiStyle &style = ImGui::GetStyle();
    const float width = ImGui::GetFrameHeight();
    const float gap = style.ItemSpacing.y;
    for (const bool right : {false, true})
    {
      std::vector<std::string> &strips = right ? state.folded_right : state.folded_left;
      if (strips.empty())
      {
        continue;
      }
      // Where each panel sits in the dock tree, whatever is folded: its dock
      // node remains (inactive) while the panel is folded.
      std::vector<Vec2> positions;
      for (std::size_t i = 0; i < strips.size(); ++i)
      {
        const ImGuiWindowSettings *settings
          = ImGui::FindWindowSettingsByID(ImHashStr(strips[i].c_str()));
        const ImGuiDockNode *node
          = settings == nullptr ? nullptr : ImGui::DockBuilderGetNode(settings->DockId);
        positions.push_back(
          node == nullptr ? Vec2 {.x = 0.0F, .y = static_cast<float>(i)} : nominal_position(*node)
        );
      }
      const std::vector<StripSlot> slots = arrange_strips(positions, right);
      std::vector<float> lengths;
      std::size_t columns = 0;
      for (std::size_t i = 0; i < strips.size(); ++i)
      {
        lengths.push_back(ImGui::CalcTextSize(strips[i].c_str()).x + (2.0F * style.FramePadding.x));
        columns = std::max(columns, slots[i].column + 1);
      }
      // Top of each strip: the strips above it in its column, plus gaps.
      std::vector<float> tops(strips.size(), 0.0F);
      float height = 0.0F;
      for (std::size_t i = 0; i < strips.size(); ++i)
      {
        for (std::size_t j = 0; j < strips.size(); ++j)
        {
          if (slots[j].column == slots[i].column && slots[j].row < slots[i].row)
          {
            tops[i] += lengths[j] + gap;
          }
        }
        height = std::max(height, tops[i] + lengths[i]);
      }
      const float total = (static_cast<float>(columns) * (width + gap)) - gap;
      const float x = right ? graph->Pos.x + graph->Size.x - total : graph->Pos.x;
      ImGui::SetNextWindowPos(ImVec2(x, graph->Pos.y));
      ImGui::SetNextWindowSize(ImVec2(total, height));
      constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
      if (ImGui::Begin(right ? "##StripsRight" : "##StripsLeft", nullptr, kFlags))
      {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        std::optional<std::size_t> unfold;
        for (std::size_t i = 0; i < strips.size(); ++i)
        {
          const float column_x = right
            ? x + total - (static_cast<float>(slots[i].column + 1) * (width + gap)) + gap
            : x + (static_cast<float>(slots[i].column) * (width + gap));
          const ImVec2 min(column_x, graph->Pos.y + tops[i]);
          const ImVec2 size(width, lengths[i]);
          ImGui::SetCursorScreenPos(min);
          ImGui::PushID(strips[i].c_str());
          if (ImGui::InvisibleButton("##strip", size))
          {
            unfold = i;
          }
          const bool hovered = ImGui::IsItemHovered();
          ImGui::PopID();
          if (hovered)
          {
            ImGui::SetTooltip("Unfold %s", strips[i].c_str());
          }
          ImDrawList &list = *ImGui::GetWindowDrawList();
          list.AddRectFilled(
            min,
            ImVec2(min.x + size.x, min.y + size.y),
            ImGui::GetColorU32(hovered ? ImGuiCol_TabHovered : ImGuiCol_Tab),
            style.TabRounding
          );
          draw_rotated_text(
            list, min, size, strips[i].c_str(), ImGui::GetColorU32(ImGuiCol_Text), right
          );
        }
        if (unfold)
        {
          strips.erase(strips.begin() + static_cast<std::ptrdiff_t>(*unfold));
        }
      }
      ImGui::End();
      ImGui::PopStyleVar(2);
    }
  }

  bool
  Ui::dock_below_graph(unsigned int dockspace_id, const char *window)
  {
    constexpr float kShare = 0.35F;
    const ImGuiDockNode *central = ImGui::DockBuilderGetCentralNode(dockspace_id);
    if (central == nullptr)
    {
      return false;
    }
    ImGuiID graph = central->ID;
    const ImGuiID below
      = ImGui::DockBuilderSplitNode(graph, ImGuiDir_Down, kShare, nullptr, &graph);
    ImGui::DockBuilderDockWindow(window, below);
    ImGui::DockBuilderFinish(dockspace_id);
    return true;
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
    ImGui::DockBuilderDockWindow("Source", dock_bottom);
    ImGui::DockBuilderFinish(dockspace_id);
  }

  void
  Ui::draw_menu_bar(ViewerState &state, const ChartView *view)
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
      ImGui::MenuItem("Member connections", nullptr, &state.kinds.member_edges);
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
      if (ImGui::MenuItem("Centre layout on selection", "C", false, state.selected.has_value()))
      {
        state.centre_requested = true;
      }
      const bool centred = view != nullptr && view->centred_group().has_value();
      if (ImGui::MenuItem("Normal layout", nullptr, false, centred))
      {
        state.normal_layout_requested = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Larger text", "Ctrl++", false, state.text_level < kMaxTextLevel))
      {
        state.text_level = step_text_level(state.text_level, 1);
      }
      if (ImGui::MenuItem("Smaller text", "Ctrl+-", false, state.text_level > kMinTextLevel))
      {
        state.text_level = step_text_level(state.text_level, -1);
      }
      if (ImGui::MenuItem("Default text size", "Ctrl+0", false, state.text_level != 0))
      {
        state.text_level = 0;
      }
      ImGui::MenuItem("Fonts...", nullptr, &state.show_fonts);
      ImGui::Separator();
      ImGui::MenuItem("Source", nullptr, &state.show_source);
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

    draw_status(state, view);
    ImGui::EndMainMenuBar();
  }

  void
  Ui::draw_explorer(ViewerState &state, const ChartView *view)
  {
    if (!state.show_explorer || side_folded(state, "Explorer"))
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
    if (!state.show_inspector || side_folded(state, "Inspector"))
    {
      return;
    }
    // The Inspector moves its own cursor with Up/Down; ImGui's navigation would
    // move focus between widgets on the same keys.
    if (ImGui::Begin("Inspector", &state.show_inspector, ImGuiWindowFlags_NoNavInputs))
    {
      if (view == nullptr || !state.selected)
      {
        text_disabled("Nothing selected. Click a node in the graph or the Explorer.");
      }
      else
      {
        const bool keyboard = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        draw_selection(*view, state, *state.selected, keyboard);
      }
    }
    ImGui::End();
  }

  void
  Ui::draw_log(ViewerState &state)
  {
    if (!state.show_log || side_folded(state, "Log"))
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
  Ui::update_source(const ViewerState &state, const ChartView *view)
  {
    if (view == nullptr || !state.selected)
    {
      return;
    }
    const HierarchyGroup &group = view->hierarchy().groups[*state.selected];
    if (group.nodes.empty())
    {
      return;
    }
    const Node &node = view->chart().nodes[group.nodes.front()];
    if (node.id == source_node_ || node.file.empty())
    {
      return;
    }
    source_node_ = node.id;
    source_first_ = node.kind == NodeKind::File ? 0 : node.line_start;
    source_last_ = node.kind == NodeKind::File ? 0 : std::max(node.line_end, node.line_start);
    source_scroll_ = true;
    open_source(node.file, state, *view);
  }

  void
  Ui::open_source(const std::string &file, const ViewerState &state, const ChartView &view)
  {
    const std::filesystem::path chart_path {state.chart_path};
    std::error_code error;
    const std::vector<std::filesystem::path> roots = {
      std::filesystem::current_path(error),
      std::filesystem::path {view.chart().source_root},
      chart_path.parent_path().parent_path(),
    };
    const auto found = resolve_source(file, roots);
    if (!found)
    {
      source_file_ = file;
      source_path_.clear();
      source_ = {};
      source_error_ = fmt::format(
        "{} not found. Start the viewer in the source tree (it looks in the working directory, "
        "the chart's source root, and the directory above .comprendo).",
        file
      );
      return;
    }
    const auto time = std::filesystem::last_write_time(*found, error);
    if (*found == source_path_ && time == source_time_ && source_error_.empty())
    {
      return;
    }
    source_file_ = file;
    source_path_ = *found;
    source_time_ = time;
    source_error_.clear();
    if (std::filesystem::file_size(*found, error) > kMaxSourceBytes)
    {
      source_ = {};
      source_error_ = fmt::format("{} is too large to show", file);
      return;
    }
    source_ = highlight_source(read_file(*found));
  }

  void
  Ui::draw_source(ViewerState &state)
  {
    // Wait until the panel's place in the layout is settled (next frame's
    // start) so it is not created floating first.
    if (!state.show_source || !source_docked_ || side_folded(state, "Source"))
    {
      return;
    }
    constexpr float kFloatingWidth = 720.0F;
    constexpr float kFloatingHeight = 320.0F;
    ImGui::SetNextWindowSize(ImVec2(kFloatingWidth, kFloatingHeight), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Source", &state.show_source))
    {
      ImGui::End();
      return;
    }
    if (source_file_.empty())
    {
      text_disabled("Select a symbol or file to see its source.");
      ImGui::End();
      return;
    }
    // Follow edits made in an editor while the file is shown.
    if (const double now = ImGui::GetTime(); now >= source_next_check_ && !source_path_.empty())
    {
      source_next_check_ = now + kSourceCheckSeconds;
      std::error_code error;
      const auto time = std::filesystem::last_write_time(source_path_, error);
      if (!error && time != source_time_)
      {
        source_ = highlight_source(read_file(source_path_));
        source_time_ = time;
      }
    }
    const std::string header = source_first_ > 0
      ? fmt::format("{}  (lines {}-{})", source_file_, source_first_, source_last_)
      : source_file_;
    ImGui::TextUnformatted(header.c_str());
    if (!source_error_.empty())
    {
      ImGui::PushTextWrapPos(0.0F);
      text_disabled(source_error_.c_str());
      ImGui::PopTextWrapPos();
      ImGui::End();
      return;
    }
    ImGui::BeginChild(
      "##code", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar
    );
    // Code reads best in a monospace font: JetBrains Mono when installed,
    // otherwise the built-in Proggy fonts, which are monospace too.
    const auto mono = fonts_.find(std::string_view {"jetbrains-mono"});
    const float current_factor = fonts_.contains(state.font) ? font_size_factor(state.font) : 1.0F;
    ImFont *font = mono == fonts_.end() ? builtin_font(state) : mono->second.first;
    const float factor = mono == fonts_.end() ? 1.0F : font_size_factor("jetbrains-mono");
    ImGui::PushFont(font, ImGui::GetStyle().FontSizeBase * factor / current_factor);
    const float line_height = ImGui::GetTextLineHeightWithSpacing();
    if (source_scroll_)
    {
      const float top = static_cast<float>(source_first_ > 0 ? source_first_ - 1 : 0) * line_height;
      ImGui::SetScrollY(std::max(0.0F, top - (ImGui::GetWindowHeight() / 3.0F)));
      source_scroll_ = false;
    }
    const std::string widest = std::to_string(source_.lines.size());
    const float gutter
      = ImGui::CalcTextSize(widest.c_str()).x + ImGui::GetStyle().ItemSpacing.x * 2.0F;
    // A faint background keeps the selected lines readable; a bar in the
    // gutter marks their extent clearly.
    const ImU32 tint = ImGui::GetColorU32(ImVec4(0.40F, 0.50F, 0.70F, 0.08F));
    const ImU32 marker = ImGui::GetColorU32(ImVec4(0.45F, 0.60F, 0.90F, 0.85F));
    constexpr float kMarkerWidth = 3.0F;
    const ImVec4 number_color = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(source_.lines.size()), line_height);
    while (clipper.Step())
    {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
      {
        const auto index = static_cast<std::size_t>(row);
        const auto number = static_cast<std::uint32_t>(row + 1);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        if (source_first_ > 0 && number >= source_first_ && number <= source_last_)
        {
          const float width = std::max(ImGui::GetContentRegionAvail().x, ImGui::GetWindowWidth())
            + ImGui::GetScrollX();
          ImDrawList &list = *ImGui::GetWindowDrawList();
          list.AddRectFilled(origin, ImVec2(origin.x + width, origin.y + line_height), tint);
          list.AddRectFilled(
            origin, ImVec2(origin.x + kMarkerWidth, origin.y + line_height), marker
          );
        }
        const std::string label = std::to_string(number);
        ImGui::SetCursorScreenPos(ImVec2(
          origin.x + gutter - ImGui::GetStyle().ItemSpacing.x
            - ImGui::CalcTextSize(label.c_str()).x,
          origin.y
        ));
        ImGui::TextColored(number_color, "%s", label.c_str());
        const std::string &line = source_.lines[index];
        if (line.empty())
        {
          continue;
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x + gutter, origin.y));
        const std::vector<Token> &tokens = source_.tokens[index];
        for (std::size_t i = 0; i < tokens.size(); ++i)
        {
          const Token &token = tokens[i];
          ImGui::PushStyleColor(ImGuiCol_Text, token_color(token.kind));
          ImGui::TextUnformatted(line.data() + token.begin, line.data() + token.end);
          ImGui::PopStyleColor();
          if (i + 1 < tokens.size())
          {
            ImGui::SameLine(0.0F, 0.0F);
          }
        }
      }
    }
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::End();
  }

  void
  Ui::draw_fonts(ViewerState &state) const
  {
    if (!state.show_fonts)
    {
      return;
    }
    constexpr std::string_view kSample = "Layout engine  ChartView::shown_edges  0123 ¿?";
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
      ImVec2(viewport->GetCenter().x, viewport->GetCenter().y),
      ImGuiCond_FirstUseEver,
      ImVec2(0.5F, 0.5F)
    );
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDocking
      | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Fonts", &state.show_fonts, kFlags))
    {
      // Non-modal but floating above the docked panels, without taking focus.
      ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
      text_disabled("Font of the panels and the graph labels:");
      for (const FontChoice &choice : font_choices())
      {
        const bool available = font_available(choice.id);
        ImGui::BeginDisabled(!available);
        const std::string name {choice.name};
        if (ImGui::RadioButton(name.c_str(), state.font == choice.id))
        {
          state.font = std::string {choice.id};
        }
        ImGui::EndDisabled();
        ImGui::Indent();
        if (!available)
        {
          text_disabled("not installed next to the viewer (fonts/)");
        }
        else
        {
          // Preview at the size the font will have once chosen; FontScaleMain
          // already carries the current font's size factor.
          const auto found = fonts_.find(choice.id);
          ImFont *preview = found == fonts_.end() ? builtin_font(state) : found->second.first;
          const float current_factor
            = fonts_.contains(state.font) ? font_size_factor(state.font) : 1.0F;
          ImGui::PushFont(
            preview, ImGui::GetStyle().FontSizeBase * font_size_factor(choice.id) / current_factor
          );
          ImGui::TextUnformatted(kSample.data(), kSample.data() + kSample.size());
          ImGui::PopFont();
        }
        ImGui::Unindent();
      }
      ImGui::Separator();
      const std::string size_text
        = fmt::format("Text size: {:.0f}%", std::round(text_scale(state.text_level) * 100.0F));
      ImGui::TextUnformatted(size_text.c_str());
      ImGui::SameLine();
      ImGui::BeginDisabled(state.text_level <= kMinTextLevel);
      if (ImGui::Button("-"))
      {
        state.text_level = step_text_level(state.text_level, -1);
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(state.text_level >= kMaxTextLevel);
      if (ImGui::Button("+"))
      {
        state.text_level = step_text_level(state.text_level, 1);
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(state.text_level == 0);
      if (ImGui::Button("Default"))
      {
        state.text_level = 0;
      }
      ImGui::EndDisabled();
      text_disabled("Ctrl++ / Ctrl+- / Ctrl+0 work anywhere. Saved for next time.");
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
      ImGui::TextUnformatted("Licensed under the Apache License v2.0 with LLVM Exceptions.");
      ImGui::Spacing();
      text_disabled(
        "Built with raylib, Dear ImGui, rlImGui, {fmt}, spdlog, CLI11, toml11, libenvpp, and "
        "SQLite. Their licenses are in THIRD_PARTY_NOTICES.md."
      );
      ImGui::PopTextWrapPos();
    }
    ImGui::End();
  }

  void
  Ui::draw_status(const ViewerState &state, const ChartView *view)
  {
    const float fps = ImGui::GetIO().Framerate;
    const std::string rate = fmt::format("{:.0f} FPS", fps);
    std::vector<std::string> variants;
    std::string details;
    if (view == nullptr)
    {
      variants = {fmt::format("no chart  |  {}", rate), rate};
      details = fmt::format("Comprendo {}\nNo chart loaded\nF11 fullscreen", COMPRENDO_VERSION);
    }
    else
    {
      const std::string file = std::filesystem::path {state.chart_path}.filename().string();
      const std::string counts
        = fmt::format("{} nodes, {} edges", view->chart().nodes.size(), view->chart().edges.size());
      const std::string grouping = fmt::format("by {}", Comprendo::to_string(view->grouping()));
      std::string zoom = fmt::format("zoom {:.3g}", state.zoom);
      if (const auto centred = view->centred_group())
      {
        zoom = fmt::format("centred on {}  |  {}", view->display_label(*centred), zoom);
      }
      variants = {
        fmt::format("{}  |  {}  |  {}  |  {}  |  {}", file, counts, grouping, zoom, rate),
        fmt::format("{}  |  {}  |  {}  |  {}", file, counts, zoom, rate),
        fmt::format("{}  |  {}  |  {}", counts, zoom, rate),
        fmt::format("{}  |  {}", zoom, rate),
        rate,
      };
      details = fmt::format(
        "Comprendo {}\n{}\n{}, {}, {}\nF11 fullscreen",
        COMPRENDO_VERSION,
        state.chart_path,
        counts,
        grouping,
        zoom
      );
    }
    const ImGuiStyle &style = ImGui::GetStyle();
    const float available = ImGui::GetContentRegionAvail().x - style.ItemSpacing.x;
    const auto fits = std::ranges::find_if(
      variants,
      [&](const std::string &text) { return ImGui::CalcTextSize(text.c_str()).x <= available; }
    );
    if (fits == variants.end())
    {
      return;
    }
    const float width = ImGui::CalcTextSize(fits->c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width);
    ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
    ImGui::TextUnformatted(fits->c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("%s", details.c_str());
    }
  }
} // namespace Comprendo
