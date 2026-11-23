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

