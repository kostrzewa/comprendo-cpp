\mainpage Comprendo CPP
\tableofcontents

# Introduction

Comprendo charts a codebase and lets you explore it. A chart is a SQLite
database (`.comprendo/chart.sqlite`) holding the project's files, symbols, and
relationships, plus explanations written by a language model. Three programs
share it:

- `comprendo-cli extract` builds the structure with Universal Ctags and, for
  C and C++, clangd-22 (resolved calls and inheritance);
- the opencode plugin's `/chart` command has the model explore the code and
  annotate the chart: summaries, details, subsystems (clusters), and missing
  symbols or relationships;
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

To annotate the chart, add the plugin to opencode (see \ref plugin) and run
`/chart` in the project. Keep the viewer open: it follows the session.

# Extraction {#extraction}

`comprendo-cli extract [--root DIR] [--output FILE] [--backend auto|ctags|clangd]`

| Backend | What it adds |
| --- | --- |
| `ctags` | Files, namespaces, classes, functions, members, macros, aliases; `Contains` tree; `Includes` edges |
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

`comprendo [CHART] [--group file|namespace|cluster] [--select NAME]`

| Input | Action |
| --- | --- |
| Mouse wheel | Zoom about the cursor |
| Left drag, right or middle drag | Pan |
| Left click | Select (Inspector shows it, Explorer reveals it) |
| Double click | Drill down: select and zoom to the group |
| Click or drag in the minimap | Move the view there (View → Minimap toggles it) |
| Hover | Tooltip: name, kind, file:line |
| `Home` | Fit the chart; it stays fitted until you zoom or pan |
| `G` | Cycle grouping: file, namespace, cluster |
| `Ctrl+F` | Filter the Explorer |
| `F11` / `Escape` | Toggle / leave fullscreen |
| `Ctrl+Q` | Quit |

View → Show hides symbol kinds (with their contents) and relationship kinds,
for example everything but calls.

Groupings:

- **File**: the containment tree, file → namespace → class → member.
- **Namespace**: namespaces merged across files.
- **Cluster**: the subsystems the model named with `/chart`, nested, each
  member with its contents; everything else under "(unclustered)".

![Cluster grouping: the subsystems named during /chart, the Viewer cluster selected](viewer_clusters.png)

The viewer polls the chart twice a second. When only explanations changed, it
takes them over in place; when nodes or edges changed, it rebuilds the view,
keeping the grouping, camera, and selection.

# opencode plugin {#plugin}

The plugin lives in `plugin/`, needs nothing besides opencode, and carries its
own copy of the chart schema, so `plugin/src` can also be copied elsewhere. Add it to the
project's `opencode.json` (or `~/.config/opencode/opencode.json`):

```json
{ "plugin": ["/path/to/comprendo-cpp/plugin/src/index.ts"] }
```

`/chart [instructions]` asks the model to run `comprendo-cli extract` when the
CLI is on `PATH`, read the code, and use the chart tools:

| Tool | Purpose |
| --- | --- |
| `chart_overview` | Counts, clusters, per-file progress |
| `chart_find_node` | Look up nodes and their ids |
| `chart_annotate_node` | Summary and detail of a node |
| `chart_annotate_cluster` | Create clusters by label and assign nodes |
| `chart_add_node`, `chart_add_edge` | Fill gaps the extractor left |
| `chart_finish` | Check the chart and print the viewer command |

# Chart format {#chart_format}

The schema is `schema/chart.sql`, shared by the C++ tools and the plugin; the
conventions (ids, kinds, provenance, validation, versioning) are described in
`plan/chart-schema.md`. Node ids are 64-bit FNV-1a hashes of kind, qualified
name, file, and signature, so the extractor and the plugin agree on them.

# Development

See `AGENTS.md` for the toolchain (clang-22 tools only), tests
(`ctest --preset clang-22`, plugin tests with `bun test` or
`BUN_BE_BUN=1 opencode test`), formatting, `clang-tidy`, and documentation
rules, and `plan/` for requirements, architecture, milestones, and the log.
