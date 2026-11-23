# Architecture

Cross-references: [requirements](comprendo-cpp.md) · [chart schema](chart-schema.md) · [milestones](milestones.md)

## Purpose

Comprendo is a code explorer and visualizer. A deterministic extractor builds the
structural graph of a codebase; the opencode model explores the codebase and
writes nodes, explanations, and semantic edges; both write the same SQLite
chart, and a raylib viewer renders it for navigation.

## Components

| Component | Language | Role |
| --- | --- | --- |
| `comprendo_core` | C++ | Chart model, SQLite store, extractors, layout |
| `comprendo` | C++ / raylib | Interactive 2D viewer |
| `comprendo-cli` | C++ | `extract`, `validate`, `info` |
| `plugin/` | TypeScript | opencode `/chart` command and chart-building tools |

The chart is a single SQLite database, `.comprendo/chart.sqlite`, described in
[chart-schema.md](chart-schema.md). There is no bridge between the plugin and
the CLI: each writes the database directly. Schema logic stays in one place
because both apply the same DDL file, `schema/chart.sql`, and both check
`application_id` and `user_version` before writing.

## Toolchain convention

Every clang-family tool is invoked with its version suffix, preferring 22:

- `clang-22`, `clang++-22` — compiler
- `clangd-22` — semantic extractor backend
- `clang-format-22` — formatting
- `clang-tidy-22` — lint (`clang-tidy` target, checks in `.clang-tidy`)
- `llvm-cov-22`, `llvm-profdata-22` — coverage

`CMakePresets.json` pins `CMAKE_C_COMPILER=clang-22` and
`CMAKE_CXX_COMPILER=clang++-22` and enables
`CMAKE_EXPORT_COMPILE_COMMANDS=ON`. The clangd backend defaults to
`clangd-22` and can be overridden through `.comprendo/config.toml` or the
`COMPRENDO_CLANGD` environment variable. Unversioned binaries on `PATH` are
never used.

## Data flow

```
                     source tree
                    /           \
                   v             v
  comprendo-cli extract       opencode plugin (/chart)
  (ctags + clangd-22)         (model exploration, bun:sqlite)
  provenance = Extractor      provenance = Model
                   \             /
                    v           v
                .comprendo/chart.sqlite   (SQLite, WAL)
                         |
                         v
                comprendo (viewer, read-only)
```

## Extractor

`Extractor` is an interface with two implementations:

- `CtagsExtractor` — complete, fast symbol inventory. Works without a compile
  database and covers every language Universal Ctags supports.
- `ClangdExtractor` — C/C++ semantics. Runs the ctags pipeline for the node
  set, then spawns `clangd-22` with `--compile-commands-dir=<build>`, speaks
  LSP over stdio, and adds `Calls` and `Inherits` edges from `callHierarchy/*`
  and `typeHierarchy/*`.

The `auto` backend (default) uses clangd when a compilation database is found
and the binary runs, and falls back to the ctags chart with a warning
otherwise; `backend = "clangd"` (or `extract --backend clangd`) makes those
failures errors. Merge rules:

- ctags supplies the full node set;
- clangd adds `Calls`, `Inherits`, and `Uses` edges, resolving overloads and
  calls through base classes to the exact chart node;
- `Includes` edges come from parsing `#include` directives;
- `Contains` edges come from ctags scope fields and file membership.

Node identity is a 64-bit FNV-1a hash over `(kind, qualified_name, file,
signature)`, implemented in-tree. This is stable across runs and distinguishes
overloads without depending on clangd exposing USRs.

### ctags backend (M2)

Pipeline, each step a separately tested unit in `comprendo_core`:

1. **Configuration** (`ExtractConfig`): defaults, then
   `<root>/.comprendo/config.toml`, then the environment (`COMPRENDO_CTAGS` and
   `COMPRENDO_CLANGD`, via libenvpp). Unknown TOML sections or keys are errors.

   ```toml
   [extract]
   roots = ["."]                                   # subdirectories to scan
   excludes = [".git/**", ".comprendo/**", "build/**"]
   languages = ["C++"]                             # ctags names; .h counts as C++
   include_paths = ["include", "src", "."]
   ctags = "ctags"
   ctags_batch = 256                               # files per ctags call
   backend = "auto"                                # auto | ctags | clangd
   clangd = "clangd-22"
   compile_commands_dir = ""                       # empty: search (see M2b)
   ```

2. **Enumeration** (`enumerate_files`): `git ls-files --cached --others
   --exclude-standard` inside a work tree, otherwise a directory walk skipping
   `.git`. Globs: `*` and `?` within a segment, `**` across segments; a pattern
   without `/` matches the file name at any depth.
3. **Language detection**: `ctags --print-language`, filtered by `languages`.
   Universal Ctags 5.9 has no `--` marker, so paths starting with `-` are
   skipped with a warning.
4. **Tags**: `ctags --output-format=json --fields=+KnSesp`, parsed with the
   in-tree `Json` parser (also used by the LSP client).
5. **Includes** (`scan_includes`, `resolve_include`): C-family files only;
   quoted includes resolve next to the includer first, then through
   `include_paths`; only project files produce edges.
6. **Chart building** (`build_ctags_chart`, pure): one File node per source
   file; one node per charted tag. Locals, parameters, labels, include
   references, and anonymous callables (ctags `__anon*` lambdas) are not
   charted. Prototypes (requested with `--kinds-C++=+p`) are matched to
   definitions with the same qualified name: identical signature, then
   identical parameter types (names and default values stripped) and
   qualifiers, then the only definition with the same arity and qualifiers.
   Matched and `= delete` prototypes are dropped, so a function declared in a
   header and defined in a source file appears once, at its definition; pure
   virtual, defaulted, and externally defined functions appear at their
   declaration. Functions in class-like scopes become methods; Python `member` tags are methods;
   enumerators are fields; unknown kinds become `Other`. Qualified names join
   the ctags scope with `::` (C family) or `.` (others). Namespaces are per
   file, so `Contains` is a tree: each symbol's parent is its enclosing scope
   (matching kind and same file preferred, across files otherwise, e.g. an
   out-of-line method under its class in the header), or its file. Duplicate
   ids keep the first tag in `(path, line)` order.
7. **Doc comments** (`DocComments`, `apply_doc_comments`): empty summaries are
   filled from Doxygen comments only (`///`, `//!`, `/** */`, `/*! */`,
   trailing `///<`; plain comments are ignored), found directly above the
   declared name (attributes, template headers, and a return type may sit in
   between) or, for a definition, above its matched prototype, so header
   documentation reaches definitions in source files. File nodes take their
   `\file` comment. `\brief` (else the first sentence) becomes the summary;
   the rest of the description, without `\param`, `\return`, and similar
   sections or `\code` blocks, becomes the detail. Summaries are capped at 200
   bytes and details at 600. Provenance stays `Extractor`.

### clangd backend (M2b)

`enrich_with_clangd` adds edges to a finished ctags chart:

1. **Compilation database** (`find_compile_commands`): `compile_commands_dir`
   when set, otherwise the root, `build/`, then the first directory below
   `build/` by name (e.g. `build/clang-22-debug`).
2. **Server**: `clangd-22 --compile-commands-dir=<dir> --background-index=false
   -j=<cores>`. Without a background index nothing is written into the project
   (no `.cache/clangd`); stderr goes to `/dev/null`. The LSP client
   (`LspClient`, over `Subprocess` pipes) is synchronous: server requests are
   answered with `null`, notifications skipped. `initialize` asks for UTF-8
   positions so columns are byte offsets.
3. **Parsing**: every C-family File node is opened with `didOpen`; one
   `documentSymbol` request per file waits for its AST, then clangd's `sync`
   extension waits for asynchronous preamble indexing. Without `sync`, calls to
   symbols declared only in headers went missing nondeterministically.
4. **Queries**: for each function and method, `prepareCallHierarchy` at the
   name's column (`find_name_column` on the ctags line) and
   `callHierarchy/outgoingCalls`; for each class, struct, and union,
   `prepareTypeHierarchy` and `typeHierarchy/supertypes`; for each class,
   struct, union, enum, type alias, field, and variable,
   `textDocument/references`. Queries run grouped
   by file because clangd keeps only a few ASTs; in chart (id) order nearly
   every query reparsed its file (2 min vs 28 s on this repository).
5. **Mapping** (`NodeLocator`): a result's URI and selection line map to the
   innermost node of an accepted kind whose line range contains it; otherwise
   to the only node whose qualified name equals clangd's `detail`. Targets
   outside the project (standard library, dependencies) are dropped.
6. **Edges**: merged per (src, dst, kind); `Calls` weight is the number of call
   sites, evidence the first call site (`file:line`); `Inherits` points from
   derived to base. Each reference outside the symbol's own definition becomes
   a `Uses` edge from the innermost function, method, or class containing it,
   except from the symbol's own members (an out-of-line `Circle::area` names
   `Circle` in its qualifier) and when an `Inherits` edge already links the
   pair (base specifiers). Provenance `Extractor`; the generator names both
   backends.

Known limitation: calls and uses involving symbols declared only in headers
without a source file (e.g. `Extractor.hpp`) occasionally differ between runs
(3 of 4600 edges on this repository), despite `sync`. clangd's single-threaded
`--sync` mode would avoid the race at several times the run time. `hover` is
not used: its text repeats the source, and `summary`/`detail` belong to the
model.

## CLI

`comprendo-cli` is a thin `main` around `run_cli` in `comprendo_core`, which
parses with CLI11 (generated `--help` for the tool and each subcommand,
`--version`). Results go to stdout; the logger (startup banner with version
and commit) goes to stderr. Exit codes: `0` success (including `--help` and
`--version`), `1` failure (invalid chart, extraction or I/O error), `2`
malformed command line. The viewer parses its options (`--smoke-test`,
`--help`, `--version`) with CLI11 as well and uses the same exit code `2` for
malformed command lines, before creating a window.

| Command | Behaviour |
| --- | --- |
| `extract [--root DIR] [--output FILE]` | Load `DIR/.comprendo/config.toml`, run the ctags extractor, write FILE (default `DIR/.comprendo/chart.sqlite`). |
| `validate [FILE]` | `check_chart_file`: `integrity_check`, `foreign_key_check`, identity/version, `validate()`; lists every problem on stderr. |
| `info [FILE]` | Header fields, node and edge counts per kind, clusters, layout. |

`FILE` defaults to `.comprendo/chart.sqlite` in the working directory. The
plugin does not call the CLI; `/chart` may instruct the model to run
`comprendo-cli extract` through its shell tool so annotations start from the
extracted nodes, which the plugin looks up by qualified name and file rather
than recomputing ids.

## Plugin

`plugin/` is an opencode plugin with no dependencies besides Bun's built-ins.
opencode loads it by path (`"plugin": ["<repo>/plugin/src/index.ts"]`), which
requires the default export `{ id, server }`. It does not invoke
`comprendo-cli` itself.

- **Command**: the `config` hook adds `/chart` unless the user defined one.
  The prompt tells the model to treat Doxygen summaries (shown as `[doc]`)
  and other comments as the authors' intent: keep accurate ones, rewrite only
  to correct or add the why, and keep explanations to one sentence and two to
  four sentences of detail regardless of comment length. `chart_overview` and
  `chart_finish` count code-documented and model-annotated symbols apart.
  The prompt has the model run `comprendo-cli extract` through its shell tool
  when the CLI is on `PATH` (this rewrites `.comprendo/chart.sqlite`), then
  explore, annotate, cluster, and call `chart_finish`.
- **Tools**: `chart_overview`, `chart_find_node`, `chart_add_node`,
  `chart_annotate_node`, `chart_annotate_cluster`, `chart_add_edge`,
  `chart_finish`. Arguments are plain JSON Schema: opencode accepts these
  next to zod schemas (marking every argument required and skipping
  validation), so handlers validate their own arguments and report mistakes
  as tool errors the model can fix. Ids appear as `0x` plus 16 hex digits.
- **Storage**: each call opens `.comprendo/chart.sqlite` with `bun:sqlite`
  (`safeIntegers`, ids as `BigInt`), refuses non-charts and newer schema
  versions, enables WAL and foreign keys, applies the schema, runs
  one immediate transaction, and closes. Holding no connection between calls
  lets `comprendo-cli extract` replace the file during a session.
- **Identity**: existing nodes are found by name and file
  (`chart_find_node`), never by recomputing ids, because extractor signatures
  use ctags' exact text. Ids of nodes the model adds use the FNV-1a recipe in
  [chart-schema.md](chart-schema.md#ids); new nodes hang under their file
  (created when missing) by a `Contains` edge, and stale layout rows are
  dropped. Clusters are addressed by label.
- **Finish**: `chart_finish` runs `integrity_check` and
  `foreign_key_check`, stamps `meta`, and prints `comprendo <chart>`. It does
  not launch the viewer.
- **Self-contained**: the plugin runs on other repositories and may be copied
  away from this one, so it reads nothing outside `plugin/src`. The schema is
  embedded in `plugin/src/schema.ts`, generated from `schema/chart.sql` by
  `plugin/scripts/embed-schema.ts`; a test fails when the copy is stale, and
  another runs the tools from a copy of `plugin/src` alone.
- **Testing**: `bun test` in `plugin/`. Without a Bun install,
  `BUN_BE_BUN=1 opencode` is Bun (opencode is a Bun executable); CMake uses
  either.

Without the CLI, the model builds the chart from scratch through
`chart_add_node`. The viewer shows rows from either writer.

## Layout

`build_hierarchy(chart, grouping)` nests nodes into groups: by file (the stored
`Contains` tree, smallest parent id when several, cycles cut) or by namespace
(named namespaces merged by qualified name, anonymous ones per file, file
contents moved to the root). `compute_layout(chart, hierarchy, options)` then
places every group as a disk, bottom-up:

1. children start on a golden-angle spiral, largest first, with SplitMix64
   jitter seeded by `options.seed ^ group.key` (no trigonometry, so results do
   not depend on libm);
2. a fixed number of iterations (fewer for groups above 1024 children, scaled
   with 1/sqrt(n)) apply Barnes–Hut repulsion weighted by disk area (`QuadTree`,
   theta 0.8), springs for non-`Contains` edges aggregated at the deepest common
   group, and linear gravity; steps are force/mass capped by a temperature
   cooling geometrically from the group extent to a tenth of the leaf spacing,
   followed by one collision sweep;
3. overlaps are removed by parallel Jacobi sweeps (each pair's overlap split by
   mass, so a large disk is not overshot by the pushes of many small ones) until
   shallow, then by an exact uniform expansion, so siblings never overlap;
4. the group radius encloses its children plus padding; absolute positions are
   assigned top-down from the origin.

`QuadTree` sorts bodies along a Morton curve (radix sort of keys packing the
42-bit Morton code above the 22-bit body index) and stores leaves of up to 16
bodies as structure-of-arrays blocks (zero-mass padding lanes). Each leaf walks
the tree once against its bounding box and evaluates the collected far cells
and near leaves with a branch-free 16-lane kernel (`m * d / |d|^2`, no square
root), which compiles to packed SSE. The simulation runs in single precision:
group-local coordinates stay below about 1e4, where float resolves 1e-3.

Groups of 2048+ bodies evaluate their leaves and sweeps on all cores; smaller
groups of the same depth are laid out concurrently. Every leaf and body writes
only its own entries and sums in a fixed order, so the result is independent of
the thread count, and debug and release builds produce identical golden files. Gravity 3 was chosen from measurements on Catch2, imgui, and raylib:
lower values leave every nesting level sparse (the slack compounds with depth),
higher values compress large groups so that overlap removal dominates.

The layout is computed by the viewer for its current grouping; the chart's
`layout` table remains an optional cache.

## Viewer

The viewer is read-only. It opens a chart read-only (positional argument,
default `.comprendo/chart.sqlite`), computes the layout for the current grouping,
and renders with raylib. Without a chart it shows a placeholder explaining how to
create one.

`ChartView` (in `comprendo_core`, no graphics dependency, unit tested) holds the
chart, hierarchy, and layout and answers the frame's questions:

- `ViewCamera`: chart/screen conversion, zoom about the mouse, pan, and fit into
  the free central dock area (`Ui` records the central node's rectangle);
- `visible()`: level of detail by screen size, walking the hierarchy from the
  root; off-screen subtrees are culled, groups under 1.5 px become points,
  containers under 8 px are drawn filled without children, so the work per frame
  follows the screen area rather than the chart size;
- `visible_edges()`: non-`Contains` edges attached to the deepest visible
  ancestor of each endpoint and merged per kind with summed weight;
- `pick()`: the deepest drawn group under the mouse, with a few pixels of
  tolerance for small groups;
- `kind_color()` and `display_label()` (anonymous namespaces show as
  "(anonymous)").

`GraphRenderer` draws in screen space (no `Camera2D`), in passes: container
fills and rings, edges, disks and points, selection and hover rings, then
labels at a fixed pixel size, placed greedily without overlap.

Shapes are anti-aliased analytically: each disk, ring, point, and line is a
single quad, and a GLSL 3.30 fragment shader sets coverage to the signed pixel
distance to the shape's edge, clamped to one pixel. The vertex texture
coordinate carries the pixel offset from the centre; the shape parameters travel
in the vertex normal, which raylib normalizes, so they are encoded as the
direction (radius, half-width, ±1) and rescaled by the last component. raylib's
default batch only feeds the attributes of its default shader, so the renderer
owns a render batch created while its shader is active. Without GLSL 3.30 it
falls back to raylib polygons whose segment count grows with the screen radius.

Interaction: mouse wheel zooms about the cursor, left-drag or right/middle-drag
pans, left-click selects, hovering shows a tooltip (label, kind, file:line).
`Home` or View → Fit chart fits the chart; the view stays fitted (also across
window resizes and dock changes) until the user zooms or pans. `G` or the View
menu cycles cluster (the default, first in the Explorer), file, and
namespace grouping, keeping the selection. The file grouping nests files in
directory groups (`HierarchyGroup::directory`), so the Explorer shows the
directory tree and the graph draws directories as disks; a directory without
files and with a single subdirectory merges into it ("include/proj");
`--group` picks the grouping at startup. The cluster grouping nests the
model's clusters (`parent_id`); a node moves into its cluster together with
its `Contains` subtree, except members of other clusters, and top-level
nodes without a cluster gather in "(unclustered)". Cluster groups use the
cluster's colour (or a palette colour when it is 0).
Input is ignored while ImGui uses the mouse or keyboard.

- resizable window (minimum 640x480); `F11` toggles borderless fullscreen at the
  monitor's native resolution and `Escape` leaves it; `Ctrl+F` is reserved for
  search;
- cluster grouping (convex-hull fills over the file layout were dropped:
  clusters span files, so hulls would overlap everything); side panel with
  summary/detail/file:line/edges;
- edge aggregation (default): each edge is lifted to the two sibling groups
  directly below its endpoints' deepest common group (`GroupEdge::lifted_*`,
  computed once per grouping), so clusters, subclusters, files, and classes
  are linked by one line per pair, drawn wider with the summed weight
  (1 + ½·log2 w, at most 4×); edges where one endpoint contains the other are
  dropped. The selection's own edges stay member to member; View → Member
  connections (`ViewFilter::member_edges`) shows all edges that way;
- search; View → Show kind filters (`ViewFilter`, applied in `visible`,
  `visible_edges`, and `pick`); double click selects and centres the object
  in the graph area with a 0.25 s ease-out move at the current zoom
  (`CameraAnimation`); Backspace returns to the view and selection before the
  last jump (`NavigationHistory`, 100 entries; selections stored by
  `HierarchyGroup::key` so they survive regrouping and reloads); Explorer and
  Inspector jumps are recorded too. WASD pans (0.9 graph sizes per second)
  and E/Q zoom about the graph centre (doubling every 0.5 s); any manual
  camera input cancels a running move;
  a minimap in the graph area's bottom-right corner (`Minimap`: the top two
  hierarchy levels plus the viewport, at least 6 px; click or drag recentres);
- Jolt particles for cosmetic interaction feedback only — never layout;
- SFML DSP-generated hover/click/zoom tones, mutable;
- live reload (`ChartWatcher`): every 0.5 s the viewer compares `PRAGMA
  data_version` on a read-only connection (in-place commits by the plugin) and
  the file's inode (the CLI deletes and rewrites the file); after 0.3 s
  without further changes it rereads the chart. If only annotations changed,
  the view takes them over and keeps its layout; otherwise it rebuilds in the
  current grouping, keeping the camera and the selection. Unreadable
  intermediate states keep the current view.

With `--profile FILE`, profiler scopes wrap each frame's input and drawing
(and the core's extraction and layout scopes); at exit the scope tree is
logged and a Perfetto trace is written to FILE. `comprendo-cli` takes the same
option. `Logger::init` logs the project version and git commit at startup.

## User interface

The viewer uses Dear ImGui (docking branch) through rlImGui for its chrome: menu
bar, dockable Explorer/Inspector/Log panels, and a status bar. `Ui::draw` gets
the `ChartView` (or null without a chart) and shares one `ViewerState` with the
graph:

- **Selection** is a hierarchy group index in `ViewerState`. Clicks in the
  Explorer or Inspector set it and request a *focus* (the graph fits the group
  and keeps it fitted until the user zooms or pans); clicks in the graph set it
  and request a *reveal* (the Explorer opens the tree path and scrolls to it).
- **Explorer**: Files/Namespaces/Clusters toggle, filter (`Ctrl+F`) showing up to 500
  ranked matches (`ChartView::search`, cached per query), otherwise the tree in
  `ChartView::display_children` order; leaf lists above 256 rows go through
  `ImGuiListClipper`, which is told to include a revealed row.
- **Inspector** (lists reworked in M8e): Detail, Contains, Outgoing, and
  Incoming are always shown, each under a heading in its own scrolling child
  (8 rows, Detail 6 lines). With the Inspector focused, Up/Down move one
  cursor through all list rows (`move_list_cursor`) and Enter selects the
  row; the window has `NoNavInputs` so ImGui's own navigation does not also
  react. Before M8: fields of the selection's first node, summary and detail
  (written by the model), declaring files of merged namespaces, children, and
  incoming/outgoing non-`Contains` edges from `ChartView::node_edges`.
- **Log**: the last 2000 messages from an spdlog ring-buffer sink attached at
  startup.
- **Status bar**: chart path, counts, grouping, zoom, FPS.

The Explorer and the graph offer two groupings of the same chart, built in the
viewer without changing the stored model:

- **By file** — the stored `Contains` tree: file → namespace (per file) →
  class → member.
- **By namespace** — namespace nodes with equal `qualified_name` (one per file
  in the chart) are merged into a single virtual group, and their children are
  re-parented under it; files then appear only as a node attribute. Anonymous
  namespaces (`__anon*`) stay per file and display as "(anonymous)".

Node ids are unaffected, so selection, the inspector, and model annotations
work identically in both views. The graph canvas
is the transparent central dock node
(`ImGuiDockNodeFlags_PassthruCentralNode`), so raylib renders the graph behind
the panels. Camera and fullscreen shortcuts are gated on
`io.WantCaptureMouse`/`WantCaptureKeyboard`. The docking layout is persisted to
`.comprendo/ui.ini`.

Pinned versions: Dear ImGui `v1.92.7-docking`, rlImGui `Raylib_5_5`.
The viewer does not set `FLAG_WINDOW_HIGHDPI`: raylib 5.5's resize callback
uses the logical window size for the viewport, which leaves unpainted strips on
scaled displays. The window is created hidden and shown after initialization so
tiling window managers (KDE/Bismuth, i3, sway) place it instead of raylib
centering it.

## Testing

Catch2 covers the chart model, id test vectors, SQLite round-trip, schema
version and identity checks, validation, ctags JSON fixtures, clangd-22 merge
fixtures (skipped when the binary is absent), layout determinism, and a CTest
integration test for `comprendo-cli validate`. The plugin is tested with
`bun test` against temporary databases; CTests gated on Bun (or opencode)
annotate a CLI-extracted chart through the plugin's tools and check it with
`comprendo-cli validate`, `comprendo-cli info`, and the viewer smoke test.
