# Architecture

Cross-references: [requirements](comprendo-cpp.md) · [chart schema](chart-schema.md) · [milestones](milestones.md)

## Purpose

Comprendo is a code explorer and visualizer. A deterministic extractor builds the
structural graph of a codebase; the opencode model annotates that graph with
explanations and semantic edges; a raylib viewer renders the result for
navigation.

## Components

| Component | Language | Role |
| --- | --- | --- |
| `comprendo_core` | C++ | Chart model, HDF5 I/O, extractors, layout |
| `comprendo` | C++ / raylib | Interactive 2D viewer |
| `comprendo-cli` | C++ | `extract`, `import`, `validate`, `info` |
| `plugin/` | TypeScript | opencode `/chart` command and chart-building tools |

The plugin never writes HDF5 itself. It writes an ephemeral JSON seed and
annotation file, then invokes `comprendo-cli import`, which is the single HDF5
writer. This keeps schema logic in one place and makes the pipeline testable
without a running opencode instance.

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
    |
    v
Extractor (ctags baseline, clangd-22 enrichment)
    |
    v
seed JSON (ephemeral)  --->  model annotations (plugin tools)
    |                                   |
    +---------------+-------------------+
                    v
          comprendo-cli import
                    |
                    v
          .comprendo/chart.h5
                    |
                    v
              comprendo (viewer)
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

## Plugin bridge

`/chart` resolves the CLI in this order: plugin option `cliPath`, environment
variable `COMPRENDO_CLI`, then `comprendo-cli` on `PATH`. It then:

1. runs `comprendo-cli extract --root . --format json` to produce the seed;
2. prompts the session to explore and annotate;
3. exposes `chart_annotate_node`, `chart_annotate_cluster`, `chart_add_edge`,
   and `chart_finish`;
4. on `chart_finish`, runs `comprendo-cli import` to write
   `.comprendo/chart.h5`;
5. prints the chart path and the viewer command. It does not launch the viewer.

Annotation state is keyed by session id and cleared on finish. Re-running
`/chart` overwrites the whole chart.

## Viewer

The viewer is read-only. It loads `.comprendo/chart.h5`, computes a
deterministic layout, and renders with raylib:

- `Camera2D` pan and zoom, kind-colored nodes, level-of-detail labels;
- resizable window (minimum 640x480); `Ctrl+F` or `F11` toggles borderless
  fullscreen at the monitor's native resolution and `Escape` leaves it; layout
  is recomputed from the current window size;
- convex-hull cluster fills, side panel with summary/detail/file:line/edges;
- search, kind filters, double-click drill-down, minimap;
- Jolt particles for cosmetic interaction feedback only — never layout;
- SFML DSP-generated hover/click/zoom tones, mutable.

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

Catch2 covers the chart model, HDF5 round-trip, validation, ctags JSON
fixtures, clangd-22 merge fixtures (skipped when the binary is absent), layout
determinism, and a CTest integration test for `comprendo-cli validate`. The
plugin is tested with vitest against a fake CLI.
