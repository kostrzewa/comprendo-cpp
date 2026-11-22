# Milestones

Cross-references: [requirements](comprendo-cpp.md) · [architecture](architecture.md) · [chart schema](chart-schema.md)

Each milestone ends with a green build and `ctest` run using the clang-22
toolchain. Commits stay small and frequent.

## M0 — Skeleton (complete)

- Root `CMakeLists.txt` and `CMakePresets.json` pinning `clang-22` /
  `clang++-22` and exporting `compile_commands.json`.
- Feature gates: `COMPRENDO_BUILD_VIEWER`, `COMPRENDO_BUILD_AUDIO`,
  `COMPRENDO_BUILD_PHYSICS`, `COMPRENDO_BUILD_TESTS`,
  `COMPRENDO_PROFILER_ENABLED`.
- Generated `Version.hpp`; startup log contains version and git commit.
- Doxygen target with project CSS and header.
- `comprendo-cli --version` / `--help`.
- `comprendo` opens a resizable raylib window showing version and commit;
  `Ctrl+F` or `F11` toggles borderless fullscreen at the monitor's native
  resolution and `Escape` leaves it.
- Catch2 smoke test wired through CTest.
- `viewer_smoke` CTest runs the viewer's hidden `--smoke-test` under `xvfb-run`,
  exercising resize and fullscreen without touching the desktop.
- `.clang-format` present; new sources pass `clang-format-22 --dry-run`.

Acceptance: `cmake --preset clang-22 && cmake --build --preset clang-22 &&
ctest --preset clang-22` succeeds.

## M1 — Chart model and HDF5 I/O

- `Chart`, `Node`, `Edge`, `Cluster` types with stable FNV-1a ids.
- `ChartIO` read/write via highfive, matching [chart-schema.md](chart-schema.md).
- Validation with actionable error messages.
- Tests: id stability, round-trip equality, every validation rule.

Acceptance: round-trip test and validation tests pass.

## M2 — ctags extractor

- `Extractor` interface and `CtagsExtractor`.
- File enumeration via `git ls-files` with fallback walk.
- ctags JSON parsing; `Contains` and `Includes` edges.
- `.comprendo/config.toml` via toml11: roots, excludes, ctags path.
- Tests on fixture trees.

Acceptance: fixture extraction produces the expected nodes and edges.

## M2b — clangd-22 semantic backend

- `ClangdExtractor` spawning `clangd-22` with `--compile-commands-dir`.
- LSP client: initialize, `documentSymbol`, `callHierarchy/*`, `references`,
  `typeHierarchy/*`, `hover`.
- Merge with ctags output; `auto` prefers clangd-22 for C/C++.
- Tests gated on `clangd-22` presence.

Acceptance: fixture call graph resolves overloads and virtual calls that ctags
collapses.

## M3 — Deterministic layout

- splitmix64-seeded initialization, fixed-iteration force-directed layout.
- Cluster-first placement, children around centroid.
- Tests: golden hash of quantized positions, bounds, no NaN.

Acceptance: identical input yields identical layout hash.

## M4 — Viewer MVP

- Load `.comprendo/chart.h5`, render nodes and edges with `Camera2D`.
- Pan, zoom, select; inspector details arrive in M4b.
- Level-of-detail labels and kind colors.

Acceptance: a real chart opens and is navigable.

## M4a — ImGui docking shell (complete)

- Dear ImGui `v1.92.7-docking` + rlImGui `Raylib_5_5` via FetchContent.
- `Ui` lifecycle and `ViewerState`.
- Dockable Explorer, Inspector, Log panels; menu bar; status bar.
- Default layout via DockBuilder; persisted to `.comprendo/ui.ini`.
- Tiling-friendly startup: the window is created hidden and shown after
  initialization; `FLAG_WINDOW_HIGHDPI` is not set.
- `viewer_smoke` exercises ImGui and docking under `xvfb-run`.

Acceptance: the viewer opens with the dock layout and the smoke test passes.

## M4b — Panels wired to the chart

- Explorer tree from chart nodes with filtering.
- Inspector shows summary/detail/file:line/edges.
- Status bar shows chart path, node/edge counts, and zoom.

Acceptance: selecting a node populates the inspector from the loaded chart.

## M5 — CLI pipeline

- `extract`, `import`, `validate`, `info` subcommands.
- JSON seed and annotation formats documented.
- CTest integration test for `validate`.

Acceptance: `extract` → annotate → `import` → `validate` runs headlessly.

## M6 — opencode plugin

- `plugin/` package with `/chart` command and chart tools.
- CLI resolution: option → `COMPRENDO_CLI` → `PATH`.
- Session-scoped annotation state; overwrite on re-run.
- vitest tests against a fake CLI.

Acceptance: `/chart` writes `.comprendo/chart.h5` and prints the viewer command.

## M7 — Polish

- Convex-hull cluster fills, search, kind filters, drill-down, minimap.
- SFML DSP tones; Jolt cosmetic particles.
- Profiler scopes and Perfetto dump; `clang-tidy-22` target.
- Documentation pass and screenshots.
