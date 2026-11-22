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

The chart is a single SQLite database, `.comprendo/chart.db`, described in
[chart-schema.md](chart-schema.md). There is no bridge between the plugin and
the CLI: each writes the database directly. Schema logic stays in one place
because both apply the same DDL file, `schema/chart.sql`, and both check
`application_id` and `user_version` before writing.

## Toolchain convention

Every clang-family tool is invoked with its version suffix, preferring 22:

- `clang-22`, `clang++-22` — compiler
- `clangd-22` — semantic extractor backend
- `clang-format-22` — formatting
- `clang-tidy-22` — optional lint target
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
                .comprendo/chart.db   (SQLite, WAL)
                         |
                         v
                comprendo (viewer, read-only)
```

## Extractor

`Extractor` is an interface with two implementations:

- `CtagsExtractor` — complete, fast symbol inventory. Works without a compile
  database and covers every language Universal Ctags supports.
- `ClangdExtractor` — C/C++ semantics. Spawns `clangd-22` with
  `--compile-commands-dir=<build>`, speaks LSP over stdio, and uses
  `textDocument/documentSymbol`, `callHierarchy/*`, `textDocument/references`,
  `typeHierarchy/*`, and `textDocument/hover`.

The `auto` backend prefers `clangd-22` for C/C++ when `compile_commands.json`
and the binary are available, and falls back to ctags otherwise. Merge rules:

- ctags supplies the full node set;
- clangd enriches `Calls`, `Uses`, and `Inherits` edges and disambiguates
  overloads;
- `Includes` edges come from parsing `#include` directives;
- `Contains` edges come from ctags scope fields and file membership.

Node identity is a 64-bit FNV-1a hash over `(kind, qualified_name, file,
signature)`, implemented in-tree. This is stable across runs and distinguishes
overloads without depending on clangd exposing USRs.

## Plugin

The plugin does not invoke `comprendo-cli`. opencode plugins run on Bun, so the
plugin opens the chart with the built-in `bun:sqlite` module. `/chart`:

1. recreates `.comprendo/chart.db`, applies `schema/chart.sql`, and sets the
   connection pragmas (`foreign_keys`, `journal_mode = WAL`);
2. prompts the session to explore and annotate the codebase;
3. exposes `chart_add_node`, `chart_annotate_node`, `chart_annotate_cluster`,
   `chart_add_edge`, and `chart_finish`; each tool call is one transaction and
   writes rows with provenance `Model`;
4. computes node ids with the FNV-1a recipe in
   [chart-schema.md](chart-schema.md#ids), using `BigInt` (`safeIntegers`);
5. on `chart_finish`, prints the chart path and the viewer command. It does
   not launch the viewer.

Re-running `/chart` overwrites the whole chart. `comprendo-cli extract` is an
independent writer of the same file; the viewer shows rows from either.

## Viewer

The viewer is read-only. It opens `.comprendo/chart.db` read-only, computes a
deterministic layout, and renders with raylib:

- `Camera2D` pan and zoom, kind-colored nodes, level-of-detail labels;
- resizable window (minimum 640x480); `Ctrl+F` or `F11` toggles borderless
  fullscreen at the monitor's native resolution and `Escape` leaves it; layout
  is recomputed from the current window size;
- convex-hull cluster fills, side panel with summary/detail/file:line/edges;
- search, kind filters, double-click drill-down, minimap;
- Jolt particles for cosmetic interaction feedback only — never layout;
- SFML DSP-generated hover/click/zoom tones, mutable;
- live reload when `PRAGMA data_version` changes while the plugin writes (M7).

Profiler scopes wrap update and render; a Perfetto trace is dumped on exit or a
hotkey. `Logger::init` logs the project version and git commit at startup.

## User interface

The viewer uses Dear ImGui (docking branch) through rlImGui for its chrome: menu
bar, dockable Explorer/Inspector/Log panels, and a status bar. The graph canvas
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
`bun test` against a temporary database; a CTest gated on `bun` validates a
plugin-written chart with `comprendo-cli validate`.
