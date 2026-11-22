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
