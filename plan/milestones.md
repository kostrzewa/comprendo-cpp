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
  `F11` toggles borderless fullscreen at the monitor's native resolution and
  `Escape` leaves it (`Ctrl+F` is reserved for search).
- Catch2 smoke test wired through CTest.
- `viewer_smoke` CTest runs the viewer's hidden `--smoke-test` under `xvfb-run`,
  exercising resize and fullscreen without touching the desktop.
- `.clang-format` present; new sources pass `clang-format-22 --dry-run`.

Acceptance: `cmake --preset clang-22 && cmake --build --preset clang-22 &&
ctest --preset clang-22` succeeds.

## M1 — Chart model and SQLite store (complete)

- `Chart`, `Node`, `Edge`, `Cluster` types with stable FNV-1a ids and
  documented test vectors.
- SQLite amalgamation via FetchContent; thin RAII wrapper (`Sqlite::Database`,
  `Statement`, `Transaction`).
- `schema/chart.sql` as the shared DDL, embedded into the generated
  `ChartSchema.hpp`.
- `write_chart` / `read_chart` in `ChartStore`, matching
  [chart-schema.md](chart-schema.md), including `application_id` and
  `user_version` checks.
- Validation with actionable error messages, mirroring the SQL constraints.
- Tests: id vectors, round-trip equality, schema identity/version rejection,
  atomic failed writes, every validation rule.

Acceptance: round-trip test and validation tests pass.

## M2 — ctags extractor (complete)

- `Extractor` interface and `CtagsExtractor`
  ([architecture](architecture.md#ctags-backend-m2)).
- File enumeration via `git ls-files` with fallback walk; glob excludes.
- In-tree `Json` parser/serializer; `run_process` (posix_spawn, both pipes
  drained with poll).
- ctags JSON parsing; kind mapping; `Contains` and `Includes` edges.
- Prototype/definition matching: declaration-only functions (pure virtual,
  defaulted, external) are charted at their declaration; `= delete` and
  lambdas are skipped. Default language set is C++ only.
- `.comprendo/config.toml` via toml11: roots, excludes, languages,
  include paths, ctags path, batch size; `COMPRENDO_CTAGS` via libenvpp.
- Tests: pure unit tests for each step (including each declaration-matching
  tier), plus the `tests/fixtures/extract_basic` tree (C++ and, opted in,
  Python; git listing and directory walk), skipped when Universal Ctags is
  absent.

Acceptance: fixture extraction produces the expected nodes and edges.

## M2b — clangd-22 semantic backend (complete)

- `ClangdExtractor`: ctags node set plus `Calls` and `Inherits` edges from
  `clangd-22` ([architecture](architecture.md#clangd-backend-m2b)).
- `Subprocess` (two-way pipes) and a minimal synchronous LSP client:
  framing, file URIs, `initialize`, `didOpen`, `documentSymbol`, `sync`,
  `callHierarchy/*`, `typeHierarchy/*`, `shutdown`.
- Compilation database discovery; config keys `backend`, `clangd`,
  `compile_commands_dir`; `COMPRENDO_CLANGD`; `extract --backend`.
- `auto` falls back to ctags with a warning; `clangd` fails instead.
- Tests: framing, reader, URIs, client against a canned server, name columns,
  database discovery, node mapping, fallback, and the fixture with real
  clangd-22 (skipped when absent).
- `references`, `hover`, and `Uses` edges moved to M7.

Acceptance (met): in the fixture, `main` calls `scale(double)` and not
`scale(int)`, `total` calls the virtual `geo::Shape::area` through a base
reference, and `geo::Circle` inherits `geo::Shape`.

## M3 — Deterministic layout (complete)

- `build_hierarchy`: file grouping (stored `Contains` tree) and namespace
  grouping (namespaces merged by qualified name), order independent, cycles cut.
- `compute_layout`: hierarchical, bottom-up disk layout per group with
  SplitMix64-seeded golden-angle spiral start, Barnes–Hut repulsion
  (`QuadTree`), springs for edges between sibling subtrees, gravity, collision
  sweeps, and exact overlap removal; see
  [architecture](architecture.md#layout).
- Large groups run fewer iterations and compute forces on all cores without
  changing the result.
- Tests: SplitMix64 vectors, hierarchy rules, Barnes–Hut accuracy against exact
  sums, geometric invariants (finite, contained, non-overlapping), order
  independence, seed sensitivity, edge attraction, 3000-sibling scale, golden
  files (`tests/golden/layout_*.txt`, identical in debug and release).

Acceptance: identical input yields identical layout; golden files match.

## M3b — Layout performance (complete)

- Blocked, vectorized Barnes–Hut: bodies sorted along a Morton curve, tree
  built top-down over that order, leaves of up to 16 bodies stored as
  structure-of-arrays blocks padded with zero-mass lanes; one tree walk per
  leaf builds an interaction list evaluated by a branch-free 16-lane kernel.
  Verified in the release binary: the kernel compiles to packed SSE
  (`subps`/`mulps`/`maxps`/`divps`, fully unrolled, no scalar fallback).
- Single precision throughout the simulation (`Vec2`, `Body`, kernel,
  options); Morton quantization stays in double.
- Leaves evaluated on all cores; parallel Jacobi overlap relaxation with
  mass-weighted pushes; independent groups of one depth laid out concurrently.
- Geometric cooling, so large groups settle before the final overlap removal.
- Tests: float kernel against a double brute-force sum, leaf evaluation order
  independence (bit-identical), coincident bodies, neighbour query with reach.

- Morton order by LSD radix sort (`radix_sort`, six 11-bit passes, constant
  digits skipped) of keys packing the 42-bit Morton code above a 22-bit body
  index; the order equals the previous (code, index) comparison sort, verified
  with the golden files.
- Fixed `morton_code`: it used 3D spreading masks, so tree splits did not read
  true quadrants (forces stayed correct, the tree was less efficient); now
  tested with hand-computed codes.

Measured in release on 16 cores, `-march=native` (file / namespace grouping),
M3 → M3b: Catch2 10k nodes 0.35 / 0.67 s → 0.064 / 0.20 s; imgui 9k nodes
0.50 / 0.50 s → 0.062 / 0.12 s; raylib 42k nodes 2.5 / 5.3 s → 0.38 / 0.50 s.
Zero overlaps in all cases. The radix sort cut raylib's sort time from
0.30 s to 0.08 s of CPU time; the Morton fix accounts for the rest of the last
step's gain.

- The `clang-22-release` preset defaults `CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS` to
  `-march=native -mtune=native`; set other flags for portable builds.
- `comprendo_core` always builds with `-ffp-contract=off`. Allowing FMA was
  measured at 0–7% faster but changes the layout (both golden files fail), so
  layouts would differ between debug and release and between CPUs.

## M4 — Viewer MVP (complete)

- Load a chart read-only (default `.comprendo/chart.sqlite`), lay it out, and
  render groups and edges in screen space (`ChartView`, `GraphRenderer`); see
  [architecture](architecture.md#viewer).
- Pan, zoom, select, hover tooltip, fit into the free dock area, grouping switch
  (`G`, View menu); inspector details arrive in M4b.
- Level-of-detail culling, points, and labels; kind colours.
- Fullscreen on `F11` only; `Ctrl+F` reserved for search.
- Tests: camera, level of detail, culling, edge aggregation, picking, colours,
  grouping switch, display labels; `viewer_smoke_chart` runs the hidden-window
  smoke test on an extracted chart (fit, zoom, pick, regroup) under `xvfb-run`.
- Verified on raylib (42k nodes): loads and lays out in 0.43 s in release.

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

## M4b — Panels wired to the chart (complete)

- Explorer tree from chart nodes (containers first, then by label; large leaf
  lists clipped to the visible rows) with a ranked filter, focused by `Ctrl+F`.
- Grouping toggle: by file (stored `Contains` tree) or by namespace (namespace
  nodes merged by qualified name in the viewer), see
  [architecture](architecture.md#user-interface).
- Inspector shows kind, name, location, signature, size, provenance, summary,
  detail, files of merged namespaces, children, and incoming/outgoing edges;
  every listed group is clickable.
- One selection shared by graph, Explorer, and Inspector: panel clicks focus the
  graph on the group, graph clicks reveal it in the Explorer tree.
- Status bar shows chart path, node/edge counts, grouping, zoom, and FPS.
- Log panel shows the viewer's log (spdlog ring-buffer sink).
- `--select NAME` selects and focuses the best match at startup.
- Tests: display order, search ranking, node edge index; the chart smoke test
  selects from the panels, focuses, filters, and regroups.

Acceptance: selecting a node populates the inspector from the loaded chart.

## M4c — Anti-aliased graph rendering (complete)

- Smooth edges of disks, rings, and lines in the graph view at every zoom:
  every shape is one quad whose GLSL 3.30 fragment shader computes coverage
  from the exact pixel distance to the edge (`GraphRenderer`); polygons remain
  as a fallback when the shader cannot be compiled.
- No change to the ImGui panels; no measurable frame-time regression on the
  42k-node raylib chart (15.2 ms vs 15.2 ms per frame on xvfb's software
  rasterizer; on a GPU the quads replace fans of up to 720 triangles).
- Smoke test runs with anti-aliasing enabled (fails otherwise when a chart is
  given) and reports the average frame time; smoke tests run unthrottled.

Acceptance: graph screenshots show smooth circle and line edges.

## M5 — CLI pipeline (complete)

- `extract` (writes `chart.sqlite` directly), `validate`, `info` subcommands;
  see [architecture](architecture.md#cli).
- Command dispatch in `comprendo_core` (`run_cli`, parsed with CLI11) with
  Catch2 tests on captured streams; `comprendo-cli` logs to stderr, results to
  stdout. The viewer's options use CLI11 too.
- `check_chart_file`: SQLite integrity and foreign-key checks plus `validate()`.
- CTest integration: `cli_validate_rejects_non_chart`, and
  `cli_extract` → `cli_validate` / `cli_info` on the fixture (gated on ctags).

Acceptance: `extract` → `validate` → `info` runs headlessly.

## M6 — opencode plugin (complete)

- `plugin/` with `/chart` (registered through the `config` hook) and seven
  chart tools ([architecture](architecture.md#plugin)); loaded by path, no
  npm dependencies.
- Writes `.comprendo/chart.sqlite` itself via `bun:sqlite`; no code
  dependency on `comprendo-cli`. The `/chart` prompt asks the model to run
  `comprendo-cli extract` via its shell tool when available, then annotates
  the extracted nodes (looked up by qualified name and file).
- FNV-1a node ids in `BigInt` for model-added nodes, checked against the
  [chart-schema.md](chart-schema.md#ids) vectors; cluster ids from labels.
- `bun test` against temporary databases; CTests (`plugin_tests`,
  `plugin_annotate`, `plugin_chart_validate`, `plugin_chart_info`,
  `viewer_smoke_plugin_chart`) gated on Bun or opencode.

Acceptance (met, except a live model session): opencode 1.19.5 loads the
plugin by path, `debug config` shows `/chart`, the build agent lists the
seven tools, a scripted session fills a CLI-extracted chart that
`comprendo-cli validate` accepts and the viewer opens, and `chart_finish`
prints the viewer command. A real `/chart` run needs a model and was left to
the user.

## M7 — Polish (in progress)

Done:

- Live reload (`ChartWatcher`): `PRAGMA data_version` on a read-only
  connection plus the file's inode, polled every 0.5 s with a 0.3 s settle
  delay. Annotation-only changes are taken over without relayout
  (`ChartView::update_annotations`); structural changes rebuild the view and
  keep grouping, camera, and selection.

- Cluster grouping (`Grouping::Cluster`, `G` cycles three groupings,
  `--group`): the model's clusters as nested groups in their colours; the
  Inspector shows a cluster's summary and a node's cluster. Replaces the
  planned convex-hull fills.

- `clang-tidy` target (`run-clang-tidy-22` over `src/` and `tests/` with the
  repository `.clang-tidy`); the code base is clean, with justified `NOLINT`s
  only for POSIX and SQLite API casts.

- Documentation pass: `docs/main.md` is a user guide (extraction, viewer,
  plugin, chart format); Doxygen runs with `EXTRACT_ALL=NO` and
  `WARN_IF_UNDOCUMENTED`, and reports no warnings.

Open:

- Kind filters, drill-down, minimap.
- clangd `Uses` edges (`references`) and hover text for node details.
- Profiler scopes and Perfetto dump.
- Screenshots for the documentation.
- SFML DSP tones; Jolt cosmetic particles.

