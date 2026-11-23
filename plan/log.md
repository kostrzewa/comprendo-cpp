# Progress log

Cross-references: [requirements](comprendo-cpp.md) · [architecture](architecture.md) · [milestones](milestones.md)

One entry per milestone: what was built, decisions taken, measurements, and the
commit. Newest last.

## Before the log (M0–M4)

- M0 skeleton, M4a docking shell; M1 chart model and SQLite store (`d948299`);
  M2 ctags extractor (`5b13091`); M5 CLI (`d85acc5`); CLI11 for argument
  parsing (`86e1bef`); M3/M3b hierarchical Barnes–Hut layout, vectorized and
  multi-threaded (`3357d57`); M4 viewer MVP (`c6bcb69`).
- Decisions recorded in [milestones](milestones.md) and
  [architecture](architecture.md): SQLite instead of HDF5, plugin writes the
  chart itself, prototypes matched to definitions, `-march=native` release
  builds with `-ffp-contract=off` for bit-identical layouts.

## 2026-11-23 — M4b panels wired to the chart

- Explorer (tree, Files/Namespaces toggle, ranked filter on `Ctrl+F`),
  Inspector (fields, summary, detail, children, edges), status bar, and Log
  panel are driven by the loaded chart; one selection shared with the graph.
- `--select NAME` opens the viewer focused on a symbol; used for screenshots
  under `xvfb-run` because no input automation tool is installed.
- Fixed: a focus request issued before ImGui reports the dock area was lost.
  Fitting and focusing now share one sticky auto-fit target that is reapplied
  whenever the graph area changes, until the user zooms or pans.
- Added milestone M4c (anti-aliased graph rendering) at the user's request.
- Commit: `c42920e`.

## 2026-11-23 — M4c anti-aliased graph rendering

- Disks, rings, points, and lines are single shader quads with analytic
  coverage (exact distance to the edge in pixels), replacing triangle fans.
- Two raylib pitfalls found from screenshots, not tests: the default batch does
  not feed the normal attribute to custom shaders (fixed with an own batch),
  and `rlNormal3f` normalizes its input (shape parameters now travel as a
  direction whose last component restores the scale).
- Frame time on the raylib chart unchanged on the software rasterizer
  (15.2 ms); smoke tests now run without the 60 FPS limiter.
- Commit: `1faa233`.

## 2026-11-23 — M2b clangd-22 semantic backend

- `comprendo-cli extract` now defaults to `auto`: ctags for the node set, then
  clangd-22 for `Calls` and `Inherits` edges when a `compile_commands.json` is
  found (root, `build/`, or a directory below `build/`).
- On this repository: 621 call and 2 inheritance edges from 520 queried
  symbols in 28 s (debug build; mostly clangd parsing 69 files that include
  raylib, ImGui, and spdlog headers).
- Two clangd behaviours found by measuring, not by tests: querying in chart
  order reparsed a file per query (2 min; fixed by grouping queries per file),
  and calls to header-only declarations were missing in some runs until the
  client waited on clangd's `sync` for preamble indexing. Three runs now give
  identical edges.
- The fixture gained `total(const geo::Shape &)` to exercise a virtual call
  through a base reference (ctags counts are now 31 nodes, 29 edges).
- Commit: `5be33e3`.

## 2026-11-23 — M6 opencode plugin

- Bun is not installed, but opencode is a Bun executable: with `BUN_BE_BUN=1`
  it is Bun 1.4.4 with `bun:sqlite` and `bun test`. CMake uses `bun` when
  present, otherwise that.
- Read from opencode's bundled source: plugins listed by path must default
  export `{ id, server }`; tool arguments may be plain JSON Schema instead of
  zod. Both let the plugin load from this repository without `npm install`.
- Tools open the chart per call, so `comprendo-cli extract` can rewrite it
  mid-session; arguments are validated in the handlers because opencode does
  not validate JSON Schema arguments.
- Verified with opencode 1.19.5 in a scratch project: `/chart` is registered
  and the build agent lists all seven tools. No live model session was run.

- Commit: `b28ad93`.

## 2026-11-23 — M7a live reload

- The viewer follows the chart while `/chart` or `comprendo-cli extract`
  writes it. `data_version` alone misses the CLI, which deletes and recreates
  the file; the inode alone misses in-place commits. The watcher uses both.
- Checked under `xvfb-run` against the plugin's tools: an annotation reloads
  in place, a model-added node rebuilds the view (6 ms on the fixture).

- Commit: `0f1e519`.

## 2026-11-23 — M7b cluster grouping

- Clusters from `/chart` became a third grouping rather than convex hulls:
  a cluster usually spans files, so hulls over the file layout would cover
  most of the chart, while a grouping lays each subsystem out as one disk.
- Checked with a screenshot of this repository's chart clustered into four
  subsystems through the plugin's tools (`--group cluster --select Viewer`).

- Commit: `ea16a7c`.

## 2026-11-23 — M7c clang-tidy target

- `.clang-tidy` and a `clang-tidy` target; 32 findings fixed. One was a real
  bug: `Logger::error` forwarded its arguments twice, so moved-from values
  could reach the exception message. The exit codes became constants instead
  of an unscoped enum.

- Commit: `5894649`.

## 2026-11-23 — M7d documentation pass

- `docs/main.md` became the user guide. The documented `doxygen` target only
  exists with `COMPRENDO_BUILD_DOXYGEN=ON`, so earlier "no warnings" checks
  had not run at all; with `EXTRACT_ALL` on, undocumented members would not
  have warned anyway. Doxygen is now strict, and the 25 missing `\return` and
  `\param` lines were added.

- Commit: `3901062`.

## 2026-11-23 — M7e kind filters, drill-down, minimap

- The minimap's viewport rectangle vanished when zoomed into a method (it was
  below a pixel); found in a screenshot, now drawn at least 6 px wide.
- Pressing in the minimap now owns the press until release, so releasing it
  does not select what lies under the minimap in the graph.

- Commit: `314e850`.

## 2026-11-23 — M7f profiling switch

- The core had profiler scopes since M3, but no binary started the profiler,
  so they recorded nothing. `--profile FILE` on `comprendo-cli` and
  `comprendo` now does; a clangd extraction of the fixture shows the clangd
  phases next to the ctags ones in Perfetto.

- Commit: `3925655`.

## 2026-11-23 — M7g clangd use edges

- `textDocument/references` for types, fields, and variables: 2379 use edges
  on this repository for about 4 s more (33 s in total, debug build).
- First results were noisy in two ways, both now filtered: out-of-line members
  "used" their own class through the qualifier, and base specifiers duplicated
  `Inherits`.
- Comparing two runs found 3 of about 4600 edges differing, all around the
  header-only `Extractor.hpp`; `sync` narrows but does not close clangd's
  indexing race. Recorded as a known limitation.

- Commit: `4236c4a`.

## 2026-11-23 — M7h screenshots, and open questions

- Two screenshots of this repository's chart (clangd edges, four clusters, a
  few annotations written through the plugin's tools) illustrate the guide.
- Left open for the user: the requirements name SFML "for anything basic not
  supported by raylib (sound, for example)", but raylib's `raudio`
  (miniaudio) is already built in and can play DSP-generated waves, so SFML
  would add a dependency for no new capability. Jolt is listed for cosmetic
  particles only. Both need a decision before work starts.

- Commit: `95fb3f6`.

## 2026-11-23 — Plugin carries its schema

- The plugin read `../../schema/chart.sql` relative to its source, which only
  works inside a comprendo checkout. It now embeds the DDL in
  `plugin/src/schema.ts`, generated by `plugin/scripts/embed-schema.ts`;
  `schema/chart.sql` remains the single source, guarded by a test that fails
  on a stale copy, and a test runs the tools from a copy of `plugin/src` alone.

- Commit: `81fc846`.

## 2026-11-23 — M8a documentation-aware charts

- Added milestone M8 at the user's request (doc comments plus eight viewer
  items).
- Extraction fills 1105 of 1544 node summaries in this repository from
  Doxygen. Most documentation here sits on header declarations while the
  nodes sit at source definitions, so matched prototypes now report their
  location (`DeclarationSite`) and the comment is taken from there.
- Only Doxygen markers are read at extraction: plain `//` blocks above
  declarations are as often commented-out code or separators. The model still
  reads plain comments while exploring, as the prompt asks.

- Commit: `6f2f1e2`.
- Commit: `5676387`.

## 2026-11-23 — M8b, M8c cluster-first viewer, directory tree

- M8b: cluster grouping is the default and listed first everywhere.
- M8c: directories are groups of the file hierarchy rather than an
  Explorer-only tree, so the list on the left and the disks on the right stay
  the same structure. The file-layout golden changed accordingly
  (`COMPRENDO_UPDATE_GOLDEN=1`); the other layout tests passed unchanged.

- Commit: `7330709`.

## 2026-11-23 — M8d aggregated connections

- Aggregation is general rather than cluster-specific: lifting each edge to
  the sibling pair below the endpoints' common ancestor gives cluster-to-
  cluster lines at the top, subcluster lines inside a cluster, and file or
  class lines further down, in every grouping. On this repository's
  clustered chart the top level shows a handful of lines instead of
  thousands.
- Keeping the selection's own edges unaggregated preserves the "what does
  this touch" question that member lines used to answer.

- Commit: `9b7a05b`.
- Commit: `2cd06d8`.

## 2026-11-23 — M8e–h Inspector lists, keyboard, animated navigation

- Items 5 and 8 of the request overlap; implemented as: double click centres
  without zooming, animated (0.25 s, ease-out), and Backspace returns,
  animated, to the previous view and selection.
- The double click's first click already changes the selection, so the view
  to return to is captured when a click sequence starts, not on the double
  click itself.
- Camera maths, cursor movement, and history are pure (`Navigation`) and unit
  tested; the smoke test drives a jump and a Backspace in the real viewer.
  Key handling itself is untested: no input automation tool is installed.

- Commit: `b4f6e87`.

## 2026-11-23 — M9a edges to off-screen groups

- Edges vanished on zoom because culled groups were treated like collapsed
  ones: the endpoint climbed to the root and the edge was dropped. A group
  not drawn below an expanded parent is now an off-screen endpoint.

- Commit: `44fef0f`.
- Commit: `26f5cf3`.
- Commit: `289bd08`.

## 2026-11-23 — M9b–d Inspector connections and height, Explorer double click

- M9b: clusters, directories, files, and namespaces list connections with
  groups of their own type; unclustered counterparts show as their files,
  which says more than one "(unclustered)" entry.
- M9c: the Inspector's sections share the window height (`share_heights`).
- M9d: Explorer rows already asked ImGui to open on double click, yet the
  user saw no unfolding; the row now toggles its open state explicitly on a
  double click that ImGui did not handle. Not reproducible here without input
  automation, so the fix is guarded to be harmless if ImGui does toggle.

- Commit: `6ce841d`.

## 2026-11-23 — M9d fix: double-click timing

- Reproduced with real X input (XTest through ctypes under xvfb): double
  clicks with about 130 ms between presses unfolded Explorer rows, 400 ms did
  not. ImGui's double-click window defaults to 0.30 s, shorter than desktop
  defaults; it is now 0.5 s. The explicit toggle added in M9d was the wrong
  diagnosis and is removed; ImGui's `OpenOnDoubleClick` does the unfolding.

- Commit: `41a5687`.

## 2026-11-23 — M9e progressive aggregation for the selection

- Selecting a cluster brought back every member edge (the M8d exception for
  the selection). Now both ends of edges crossing the selection are refined to
  one level below it, as agreed with the user: cluster → files, file →
  classes, class → methods, leaf → member detail.
- The selection's subtree is marked once per frame instead of calling
  `contains()` per edge.

- Commit: `7483dc3`.

## 2026-11-23 — M9f camera-independent connections

- Line ends were still attached to drawn groups (M9a only handled culled
  groups below an expanded parent), so zooming out or panning merged or
  dropped lines. `shown_edges` now takes no draw list at all; lines end at
  the chosen groups' positions wherever they are. Two tests that asserted
  zoom-dependent merging were replaced by one asserting exact endpoints.

- Commit: `a57fa9e`.

## 2026-11-23 — M9g internal lines along the selection's path

- Measured on the user's chart (1509 nodes, 4662 edges): of ~1000 lines drawn
  with Tests selected, all but ~20 were inside groups at every level; 113
  were the selection's crossing lines. Internal lines are now drawn only
  inside the root, the selection, and its ancestors. Remaining density with a
  cluster selected comes from the crossing lines (proposed next: end them at
  the selection's peers, ~113 → ~30 for Tests).

- Commit: `3af5403`.

## 2026-11-23 — M9h crossing lines end at the selection's peers

- The user's screenshot with Tests selected was still dense: every test file
  linked to individual files in other clusters. The far end of a crossing
  line is now at the selection's own depth, revising M9e's "same depth"
  choice, which fans out on real charts.

- Commit: `7347d1d`.

## 2026-11-23 — M9i one line per pair

- Uses, calls, and includes between two groups, in both directions, merge
  into one line (colour = heaviest kind). The overview of the user's chart is
  now one line per cluster pair; with Tests selected about 60-80 links remain,
  real ones (20 test files reaching 3-5 clusters each).

- Commit: `62c883f`.

## 2026-11-23 — Inheritance from ctags

- The user noticed `geo::Shape` (a base class in the test fixture) had no
  connections: only clangd recorded inheritance, and clangd never sees files
  outside `compile_commands.json`. ctags reports bases (`inherits`, field
  `i`), which now become `Inherits` edges; clangd skips edges ctags already
  recorded, since a chart may not hold duplicates.

- Commit: `23a9450`.

## 2026-11-23 — M9j bundling and hover emphasis

- The user chose bundling and hover detail. Outside the selected cluster,
  only one trunk per destination remains; inside, fibres meet at the rim.
  Checked with screenshots of the user's chart, hovering through XTest.

- Commit: `592c8e6`.

## 2026-11-23 — Threads per physical core

- At the user's request, layout and clangd use one thread per physical core
  instead of per hardware thread (8 instead of 16 here), read from the sysfs
  topology of the CPUs in the affinity mask.

