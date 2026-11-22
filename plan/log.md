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
