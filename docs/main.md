\mainpage Comprendo CPP
\tableofcontents

# Introduction

Comprendo charts a codebase and lets you explore it. A chart is a SQLite
database (`.comprendo/chart.sqlite`) holding the project's files, symbols, and
relationships, plus explanations written by a language model. Three programs
share it:

- `comprendo-cli extract` builds the structure with Universal Ctags and, for
  C and C++, clangd-22 (resolved calls and inheritance);
- `/chart` in Claude Code or opencode has the model explore the code and
  annotate the chart through the chart tools of `comprendo-cli mcp`:
  summaries, details, subsystems (clusters), and missing symbols or
  relationships;
- `comprendo`, the viewer, draws the chart as nested disks with an Explorer,
  an Inspector, and a Log, and reloads it while the other two write.

![This repository in the viewer: ChartView selected, with the model's summary and detail and its clangd use edges in the Inspector](viewer_files.png)

# Quick start

```sh
cmake --preset clang-22-release
cmake --build --preset clang-22-release -j"$(nproc)"

cd /path/to/project
comprendo-cli extract                       # writes .comprendo/chart.sqlite
comprendo                                   # opens it
```

To annotate the chart, put `comprendo-cli` on `PATH`, add the plugin to
Claude Code or opencode (see \ref plugin), and run `/comprendo:chart` (Claude
Code) or `/chart` (opencode) in the project. Keep the viewer open: it follows
the session.

# Extraction {#extraction}

`comprendo-cli extract [--root DIR] [--output FILE] [--backend auto|ctags|clangd]`

| Backend | What it adds |
| --- | --- |
| `ctags` | Files, namespaces, classes, functions, members, macros, aliases; `Contains` tree; `Includes` and `Inherits` edges |
| `clangd` | Everything from `ctags`, plus `Calls` (overloads and virtual calls resolved), `Inherits`, and `Uses` (types, fields, variables) edges from clangd-22 |
| `auto` (default) | `clangd` when `compile_commands.json` is found and clangd runs, otherwise `ctags` with a warning |

Both backends copy Doxygen comments (`///`, `//!`, `/** */`, `///<`) into the
chart: `\brief` or the first sentence becomes a symbol's summary, the rest of
the description its detail. Documentation on a header declaration reaches the
definition in the source file. `/chart` builds on these summaries instead of
starting from nothing.

The compilation database is looked up in the root, `build/`, then the first
directory below `build/`. clangd runs without a background index, so nothing is
written into the project besides the chart. On this repository, the clangd pass
takes about 30 s (debug build), most of it clangd parsing.

Settings live in `.comprendo/config.toml`:

```toml
[extract]
roots = ["."]
excludes = [".git/**", ".comprendo/**", "build/**"]
languages = ["C++"]                 # ctags language names; .h counts as C++
include_paths = ["include", "src", "."]
ctags = "ctags"
ctags_batch = 256
backend = "auto"                    # auto | ctags | clangd
clangd = "clangd-22"
compile_commands_dir = ""           # empty: search
```

`COMPRENDO_CTAGS` and `COMPRENDO_CLANGD` override the executables.

`comprendo-cli --profile FILE ...` and `comprendo --profile FILE` log a
profile of the run and write a Perfetto trace (open it at ui.perfetto.dev).

`comprendo-cli validate [FILE]` checks a chart (exit code 1 when invalid);
`comprendo-cli info [FILE]` summarizes it.

# Viewer {#viewer}

`comprendo [CHART] [--group cluster|file|namespace] [--select NAME]`

| Input | Action |
| --- | --- |
| Mouse wheel | Zoom about the cursor |
| Left drag, right or middle drag | Pan |
| Left click | Select (Inspector shows it, Explorer reveals it, the Source panel follows it) |
| Click in the Explorer or Inspector | Select and glide to the object, zooming to fit it |
| Double click | Select and centre the object with a short animation (zoom unchanged) |
| `Backspace` | Go back to the view and selection before the last jump (double click, Explorer, Inspector); works while a panel has focus, except when typing in a text field |
| `W` `A` `S` `D` | Pan |
| `E` / `Q` | Zoom in / out |
| Click or drag in the minimap | Move the view there (View → Minimap toggles it) |
| Hover | Tooltip: name, kind, file:line |
| `Home` | Fit the chart; it stays fitted until you zoom or pan |
| `C` | Re-centre the layout on the selection: it moves to the middle of the chart with everything else arranged around it (only the groups enclosing it are laid out again, animated); `C` again, or with nothing selected, returns to the normal layout (also View → Centre layout on selection / Normal layout) |
| `G` | Cycle grouping: cluster, file, namespace |
| Double click in the Explorer | Unfold or fold a directory, cluster, file, ... |
| `Ctrl+F` | Filter the Explorer |
| `Up` / `Down`, `Enter` (Inspector focused) | Move through the Inspector's lists; jump to the highlighted entry |
| `Right` / `Left` (Inspector focused) | Unfold / fold the highlighted row of Contains |
| `Ctrl` + `+` / `Ctrl` + `-` / `Ctrl` + `0` | Larger / smaller / default text in the panels and graph labels (also View menu; keypad works too, and so does `Ctrl` + `=`) |
| ▼ on a docked panel's tab bar | Fold the panel: beside the graph (left or right) it becomes a thin strip with its name at the graph's top corner, arranged like the panels were (stacked panels give stacked strips, panels side by side give strips side by side) and the graph takes the space; above or below the graph it folds to its tab. Click the strip, or ▼ again, to unfold |
| View → Fonts... | Floating dialog: choose the font of the panels and graph labels (Proggy, Atkinson Hyperlegible, Inter, JetBrains Mono) and the text size; changes apply at once |
| `F11` / `Escape` | Toggle / leave fullscreen |
| `Ctrl+Q` | Quit |

Connections are aggregated by default: a line between two clusters (or two
subclusters, files, classes, ...) stands for every relationship between their
contents, in either direction and of any kind, thicker the more there are and
coloured by the most common kind; the Inspector lists them by kind. Lines inside a group are drawn only
for the selected group and the groups enclosing it, so with nothing selected
only the lines between top-level clusters are shown. Which lines are drawn never depends on zooming
or panning: lines to groups off screen or too small to see still point at
them. Selecting an object refines its
connections by one level: a selected cluster links its files to the other
clusters, a selected file links its classes to other files, and a selected
method or other leaf links to the members it uses. These links are bundled:
thin fibres run from the parts to the selection's rim, and one trunk per
destination continues from there. All lines run from rim to rim, and a
small dot on the rim marks where each connection leaves a group. Lines between the selection's own parts
appear only while you hover over one of them. Hovering over anything with
connections highlights them (and the trunks they feed) and dims the rest. View → Member connections draws every relationship
between its endpoints instead.

View → Source opens the Source panel, docked below the graph: the selected
symbol's file with C++ highlighting and line numbers, its lines tinted and
scrolled into view. The panel follows every selection (graph, Explorer,
Inspector) even while it is closed, so it shows the current selection when
opened; selecting never opens it. Files are looked up relative to the
working directory (start the viewer in the source tree), then the chart's
recorded source root, then the directory holding `.comprendo/`; the panel
follows edits on disk.

The status (chart file, counts, grouping, zoom, frame rate) sits at the right
end of the menu bar; hover over it for the full chart path. The Log panel is
hidden by default (View → Log).

The font, the text size, and which panels and the minimap are shown are
remembered per user in
`$XDG_CONFIG_HOME/comprendo/viewer.toml` (usually
`~/.config/comprendo/viewer.toml`). The fonts are fetched at build time and
copied to `fonts/` next to the viewer; a font whose file is missing is shown
as unavailable.

For a cluster, directory, file, or namespace the Inspector lists its
connections with other groups of the same kind (other clusters, other files)
and how many relationships each stands for. Contains shows the selection's
contents as a tree: nested groups of the same kind (subclusters,
subdirectories) start unfolded, everything else folded. The arrow folds or
unfolds a row; clicking its name selects it.

View → Show hides symbol kinds (with their contents) and relationship kinds,
for example everything but calls.

Groupings:

- **Cluster** (default): the subsystems the model named with `/chart`,
  nested, each member with its contents; everything else under
  "(unclustered)". A chart without clusters looks like the file grouping.
- **File**: directories → files → namespaces → classes → members. A
  directory whose only content is one subdirectory is shown merged with it
  ("include/proj").
- **Namespace**: namespaces merged across files.

![Cluster grouping: the subsystems named during /chart, the Viewer cluster selected](viewer_clusters.png)

The viewer polls the chart twice a second. When only explanations changed, it
takes them over in place; when nodes or edges changed, it rebuilds the view,
keeping the grouping, camera, and selection.

# Assistants: Claude Code and opencode {#plugin}

`/chart` asks the model to run `comprendo-cli extract`, read the code, and
fill the chart through seven tools. The tools are served by
`comprendo-cli mcp` (an MCP server on stdio), so every assistant uses the same
implementation; the plugins only add the command and start the server.
`comprendo-cli` must be on `PATH`, or `COMPRENDO_CLI` must name it, for
example `export COMPRENDO_CLI=/path/to/build/comprendo-cli`.

| Assistant | Install | Command |
| --- | --- | --- |
| Claude Code | `claude plugin marketplace add /path/to/comprendo-cpp`, then `claude plugin install comprendo@comprendo` (or `claude --plugin-dir /path/to/comprendo-cpp/claude_plugin` for one session) | `/comprendo:chart [instructions]` |
| opencode 1.x | `{ "plugin": ["/path/to/comprendo-cpp/opencode_plugin/src/index.ts"] }` in `opencode.json` or `~/.config/opencode/opencode.json` | `/chart [instructions]` |
| opencode 2.x | `{ "plugins": ["/path/to/comprendo-cpp/opencode_v2_plugin"] }` (the directory) | `/chart [instructions]` |

The plugins run on other repositories: they only need `comprendo-cli`, which
carries the chart schema. A user-defined `/chart` command or `comprendo` MCP
server is left alone.

| Tool | Purpose |
| --- | --- |
| `chart_overview` | Counts, clusters, per-file progress |
| `chart_find_node` | Look up nodes and their ids |
| `chart_annotate_node` | Summary and detail of a node |
| `chart_annotate_cluster` | Create clusters by label and assign nodes |
| `chart_add_node`, `chart_add_edge` | Fill gaps the extractor left |
| `chart_finish` | Check the chart and print the viewer command |

Assistants show the tools with a prefix (`mcp__plugin_comprendo_chart__…` in
Claude Code, `comprendo_…` in opencode 1.x, `tools.comprendo.…` in opencode
2.x's Code Mode). Every call opens the chart, runs one transaction, and closes
it, so `comprendo-cli extract` and the viewer can use the file in between; a
failed call writes nothing.

# Chart format {#chart_format}

The schema is `schema/chart.sql`, embedded into the C++ tools; the
conventions (ids, kinds, provenance, validation, versioning) are described in
`plan/chart-schema.md`. Node ids are 64-bit FNV-1a hashes of kind, qualified
name, file, and signature, so the extractor and the chart tools agree on them.

# Development

See `AGENTS.md` for the toolchain (clang-22 tools only), tests
(`ctest --preset clang-22`, opencode plugin tests with `bun test` or
`BUN_BE_BUN=1 opencode test`), formatting, `clang-tidy`, and documentation
rules, and `plan/` for requirements, architecture, milestones, and the log.

# License

Comprendo is licensed under the Apache License v2.0 with LLVM Exceptions
(SPDX `Apache-2.0 WITH LLVM-exception`); see `LICENSE` in the repository root.

# Acknowledgements

Comprendo is built on raylib, Dear ImGui, rlImGui, {fmt}, spdlog, CLI11,
toml11, libenvpp, and SQLite, tested with Catch2, and documented with Doxygen
and Doxygen Awesome; extraction relies on Universal Ctags and clangd from the
LLVM Project. Thank you to their authors and contributors. Versions, licenses,
and license texts are in `THIRD_PARTY_NOTICES.md`, which must accompany
binary distributions.
