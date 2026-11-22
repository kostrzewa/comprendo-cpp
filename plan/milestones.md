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

## M2b — clangd-22 semantic backend

- `ClangdExtractor` spawning `clangd-22` with `--compile-commands-dir`.
- LSP client: initialize, `documentSymbol`, `callHierarchy/*`, `references`,
  `typeHierarchy/*`, `hover`.
- Merge with ctags output; `auto` prefers clangd-22 for C/C++.
- Tests gated on `clangd-22` presence.

Acceptance: fixture call graph resolves overloads and virtual calls that ctags
collapses.

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

## M4 — Viewer MVP

- Load `.comprendo/chart.sqlite` read-only, render nodes and edges with `Camera2D`.
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
- Grouping toggle: by file (stored `Contains` tree) or by namespace (namespace
  nodes merged by qualified name in the viewer), see
  [architecture](architecture.md#user-interface).
- Inspector shows summary/detail/file:line/edges.
- Status bar shows chart path, node/edge counts, and zoom.

Acceptance: selecting a node populates the inspector from the loaded chart.

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

## M6 — opencode plugin

- `plugin/` package with `/chart` command and chart tools.
- Writes `.comprendo/chart.sqlite` itself via `bun:sqlite`; no code
  dependency on `comprendo-cli`. The `/chart` prompt asks the model to run
  `comprendo-cli extract` via its shell tool when available, then annotates
  the extracted nodes (looked up by qualified name and file).
- FNV-1a node ids in `BigInt` for model-added nodes, checked against the
  [chart-schema.md](chart-schema.md#ids) vectors.
- `bun test` against a temporary database; a CTest gated on `bun` runs
  `comprendo-cli validate` on a plugin-written chart.

Acceptance: `/chart` fills `.comprendo/chart.sqlite`, the viewer opens it, and the
plugin prints the viewer command.

## M7 — Polish

- Convex-hull cluster fills, search, kind filters, drill-down, minimap.
- SFML DSP tones; Jolt cosmetic particles.
- Live reload on `PRAGMA data_version` change.
- Profiler scopes and Perfetto dump; `clang-tidy-22` target.
- Documentation pass and screenshots.
