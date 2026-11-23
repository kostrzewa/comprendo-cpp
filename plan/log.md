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

- Commit: `0d9a3a6`.

## 2026-11-23 — M9k inner lines on hover

- Lines between the selection's children are hover-only; hovering the
  selection's background or an enclosing group does not count, or resting
  the mouse on the cluster would bring them all back.

- Commit: `5abac12`.

## 2026-11-23 — M10 opencode 2.x plugin

- The installed opencode (1.19.5) runs v2 plugins only inside its v2 runtime
  (`opencode debug v2`), whose preview context has no tool registry; the
  2.x docs (opencode.ai/docs/build/plugins/migrate-v1,
  opencode.ai/v2/docs/build/plugins) add `ctx.tool.transform`.
- `plugin_v2/` is an adapter over `plugin/src`, exporting a plain
  `{ id, setup }` so it needs no `@opencode/plugin` dependency. Command
  registration supports the documented `add` and the preview's `update`;
  tool errors are returned as content because 2.x does not document them.
- Not yet run on opencode 2.x: installing it was left to the user.

- Commit: `386073f`.

## 2026-11-23 — M10 verified on opencode 2.2.7

- Tested only with `--standalone` (private server), in a scratch project, at
  the user's request; the global config was not modified (checked by mtime
  and its v1 `plugin` key). Test sessions were removed afterwards.
- Findings: plugin commands must provide `execute` (template-only commands
  fail when invoked); `execute` receives `{ sessionID, prompt: { text },
  delivery }`, and `ctx.session.prompt({ sessionID, text, delivery })` sends a
  prompt. Tools run through Code Mode by default. `/chart` now prompts the
  session with the filled-in template.
- The global v1 `plugin` entry (a file path) is rejected by 2.x ("configured
  plugin path must be a directory"); use `plugins` with `plugin_v2` there.

- Commit: `051f719`.

## 2026-11-23 — M9l lines end at the rims

- User request: with bundling in place, the aggregated lines need not run to
  the clusters' centres. Straight lines and trunks are now clipped at the
  rims of both disks (`Comprendo::rim_to_rim`, tested in
  `test_chart_view.cpp`); touching disks keep a 6 px stub, nested disks run
  from the inner rim to the outer centre. Fibres are unchanged.
- Checked under xvfb on this repository's chart: nothing selected, a cluster
  (`Layout engine`), and a field (`layout`).

- Commit: `f66641e`.

## 2026-11-23 — M9m fibres from the rims, ports

- User request: lines of circles inside circles should also leave from the
  rim, with a small node marking the spot. Fibres now start at the children's
  rims; every rim end of a line, fibre, or trunk gets a port dot in the line's
  colour (slightly more opaque, sized by the line width), drawn after all
  lines. Nested straight lines (inner rim to outer centre) get no ports.
- Checked under xvfb: nothing selected and `Layout engine` selected.

- Commit: `4613dd3`.

## 2026-11-23 — M9n Contains as a tree

- User request: a cluster holding subclusters and members should show them as
  a tree in the Inspector's Contains list. `ChartView::contents_tree` flattens
  the visible tree into rows (depth, expandable, expanded) so the list keeps
  its clipper, keyboard cursor, and height sharing; children of the
  selection's type start unfolded. Rows are coloured like the Explorer; the
  arrow folds, the label selects, Right/Left fold with the keyboard. The
  heading counts direct children.
- Checked under xvfb with XTest clicks on `Extraction pipeline`: unfolding a
  file and folding a subcluster leave the selection unchanged.

- Commit: `eea8f34`.

## 2026-11-23 — M9o reliable Backspace history

- The user found going back unreliable. Exercised it under xvfb with XTest
  clicks and key presses on this repository's chart, with temporary
  navigation logging (removed afterwards). Found three bugs:
  - the release of a double click's second press re-picked the selection
    under the already moving camera, usually clearing it, so history entries
    recorded the wrong selection (going back restored "nothing");
  - after an Explorer click, ImGui's keyboard navigation kept
    `WantCaptureKeyboard` set, swallowing Backspace until the graph was
    clicked; Backspace is now read unless ImGui wants text input;
  - jumps during a camera move recorded the half-way pose; they now record
    where the move is heading (`settled_pose`).
- Verified: two graph double clicks then Backspace ×3, Explorer clicks with
  the Explorer focused, an Explorer double click (one entry), Inspector row
  clicks, a second jump 0.15 s into the first one's move (back lands on the
  first jump's end pose exactly), and Backspace inside the filter (edits the
  text, no navigation). A harness artefact on the way: a release and a press
  in the same frame are seen as one held press by raylib.

- Commit: `21243a3`.

## 2026-11-23 — M11 /chart for Claude Code, one C++ MCP server

- Request: `/chart` in Claude Code too. Claude Code has no Bun to lend and
  this machine has neither bun nor node, so the chart tools moved to C++ and
  are served by `comprendo-cli mcp`; the user chose to switch opencode to the
  same server and retire the TypeScript tools.
- `ChartTools` ports `chart.ts`/`tools.ts`/`ids.ts` (SQL failures stick to a
  per-call store and roll back; argument errors use the TS messages).
  `serve_mcp` speaks newline-delimited JSON-RPC (protocol revisions
  2024-11-05 … 2025-11-25). `run_cli` takes an input stream for it.
- Claude plugin: skill `/comprendo:chart` (plugin skills are namespaced;
  `disable-model-invocation`), `.mcp.json` with
  `${COMPRENDO_CLI:-comprendo-cli}` and `${CLAUDE_PROJECT_DIR}`; marketplace
  at the repository root. `claude plugin validate` passes (strict mode only
  wants an icon/licence for directory submission).
- opencode: the 1.x `config` hook can add `mcp.comprendo` (probe: connected in
  `opencode mcp list`). opencode 2.x plugins get `ctx.mcp.transform` with an
  editor offering `get/set/update/remove/list`; transforms run lazily when the
  MCP registry is built, not during `setup`. `opencode mcp list`/`api
  mcp.list` in 2.x do not load project plugins, so the check was a
  `--standalone` session (one model call) whose Code Mode call reached the
  server; the session was removed afterwards. 2.x finds the project through
  git (a scratch project without `.git` did not load its config).
- The prompt now requires the CLI (the tools run in it) and mentions the tool
  name prefixes hosts add. `comprendo-cli` is not on PATH on this machine:
  checks used `COMPRENDO_CLI`.
- Config (`~/.config/opencode/opencode.jsonc`) untouched by these runs
  (mtime 18:12 before and after; it had been edited earlier in the day).

- Commit: `986019b`.

## 2026-11-23 — Icon and license

- Icon: a blocky black-on-white `¿?` drawn on a 5×9 grid with one-cell
  strokes (`¿` is `?` turned 180°, so the pair is point-symmetric); no font,
  so nothing to attribute. Source `docs/graphics/comprendo-icon.svg`,
  rendered to `claude_plugin/assets/icon.png` (1024², 8-bit grayscale) and
  referenced by `plugin.json`. Two denser drafts read as pixel art and were
  dropped.
- License: Apache License v2.0 with LLVM Exceptions in `LICENSE` (the LLVM
  project's text without its third-party and legacy-license sections, header
  naming Comprendo); SPDX `Apache-2.0 WITH LLVM-exception` in `plugin.json`
  and both opencode `package.json` files.
- `claude plugin validate --strict` now only warns about the missing
  `author`.

- Commit: `0007433`.

## 2026-11-23 — Third-party notices

- `THIRD_PARTY_NOTICES.md`: every FetchContent dependency with version, use,
  and license, plus verbatim license texts taken from the fetched sources
  (SFML and Jolt, off by default, from their pinned tags); libenvpp's
  Apache NOTICE; SQLite's blessing; GLFW's license (bundled with raylib);
  tools that are run but not linked (Universal Ctags, clangd/LLVM, Doxygen,
  the assistants). Required with binary distributions (MIT/BSD/Apache
  notices of statically linked libraries); not for the source repository,
  which vendors nothing.
- CTest `third_party_notices` (`cmake/check_notices.cmake`) fails when a
  `FetchContent_Declare` has no `<!-- fetchcontent: name -->` entry (checked
  with a copy missing `fmt`). AGENTS.md asks for an entry per new dependency.
- Acknowledgements section in `docs/main.md`; the viewer's About dialog
  names the license and the libraries and points to the notices.
- `plugin.json` gained `author` (the user's edit, JSON typo fixed);
  `claude plugin validate --strict` passes.

- Commit: `4dce511`.

## 2026-11-23 — Plugin directories renamed

- `plugin/` → `opencode_plugin/`, `plugin_v2/` → `opencode_v2_plugin/`
  (`git mv`); references updated everywhere except this log's history.
  CTests are now `opencode_plugin_tests` and `opencode_v2_plugin_tests`.
- opencode 1.19.5 loads the plugin from the new path (scratch project,
  `mcp list`: connected). Configs pointing at the old paths must be updated
  by their owners; `~/.config/opencode/opencode.jsonc` was not touched.

- Commit: `a4f734a`.

## 2026-11-23 — M12a adjustable text size

- Ctrl++ / Ctrl+- / Ctrl+0 (and View → Larger/Smaller/Default text) change
  the text size of the panels and graph labels in levels of ×1.125.
- rlImGui at `Raylib_5_5` already sets `RendererHasTextures`, so ImGui 1.92
  rasterizes fonts at any size on demand: the level only sets
  `style.FontScaleMain` and switches between the 13 px pixel font (default
  size) and ImGui's embedded vector font (other sizes). Labels use raylib's
  default font at a scaled size.
- GLFW key codes are US-layout positions (German + arrives as `]`), so keys
  are matched by `glfwGetKeyName` (raylib's bundled GLFW; include path and
  `GLFW_INCLUDE_NONE` added to the viewer target), with a US fallback.
- Tests: level stepping/clamping/scale and character mapping
  (`test_navigation.cpp`); the smoke test checks the panel font grows to
  round(13 × 1.125²) = 16 px and returns to 13 px (ImGui rounds font sizes
  to whole pixels). Checked with XTest Ctrl+= / Ctrl+- under xvfb.
- Designed for the planned font choice (M12b): the text level is
  independent of which font is selected.

- Commit: `f9a7351`.

## 2026-11-23 — M12b/c font choice and remembered settings

- View → Fonts... opens a floating ImGui window (`NoDocking`, brought to the
  display front each frame so it stays above the docked panels without
  stealing focus): radio buttons per font with a preview in that font at
  its real size, and text size −/+/Default. Choices apply on the next frame.
- Fonts: Atkinson Hyperlegible (static), Inter and JetBrains Mono
  (variable; ImGui and raylib render the default instance), from
  google/fonts at 06bfe0d as `FetchContent` URL downloads with SHA-256,
  copied to `<build>/fonts`; OFL texts in THIRD_PARTY_NOTICES.md. Inter's
  own release archive is 33 MB, hence single files from google/fonts.
- Graph labels use the chosen TTF through raylib (`LoadFontEx` at the exact
  label size, Latin-1 plus dashes, ellipsis, arrow), reloaded only when file
  or size change; the built-in choice keeps raylib's default font.
- `ViewerSettings` (core): TOML parse/format (unknown keys ignored, unknown
  fonts rejected, level clamped), XDG path, atomic save; tests in
  `test_viewer_settings.cpp`. The smoke test switches to Inter and checks
  the panel font (Inter-Variable.ttf at 14 px).
- Checked under xvfb with an isolated XDG_CONFIG_HOME: dialog, live switch
  to Inter (panels, dialog, labels), saved file, restored after restart.

- Commit: `a10a2d0`.

## 2026-11-23 — M12d remembered panels, status in the menu bar

- User report: the Log came back on every start, and the bottom status bar
  was covered by docked panels while itself covering the minimap.
- `ViewerSettings` gained `show_explorer`, `show_inspector`, `show_log`
  (default false), `show_minimap`; the viewer loads them at startup and saves
  on change (the panels' × buttons and the View menu share the flags).
- `Ui::draw_status` draws the status right-aligned inside the main menu bar,
  choosing the most detailed of several variants that fits (file, counts,
  grouping, zoom, FPS → down to FPS), with a tooltip holding the full path
  and version. The `##StatusBar` overlay is gone; `panels_created` (smoke
  test) checks Explorer and Inspector only.
- Checked under xvfb with an isolated XDG_CONFIG_HOME: default start without
  Log, status in the menu bar, minimap uncovered; Log toggled on and off via
  the View menu survives restarts both ways.

- Commit: `3d7c5e5`.

## 2026-11-23 — M12e window icon

- The viewer sets the ¿? icon on its window: CMake copies
  `claude_plugin/assets/icon.png` to `<build>/comprendo-icon.png`; at startup
  it is converted to RGBA, resized to 16, 32, 48, 128, and 256 px, and passed
  to `SetWindowIcons` while the window is still hidden. A missing file only
  logs a warning.
- Verified under xvfb: `xprop _NET_WM_ICON` on the window lists all five
  sizes, and the decoded 48 px icon shows ¿?.

- Commit: `6585753`.

## 2026-11-23 — M12f Source panel

- Selection (single click in graph, Explorer, or Inspector; the user chose
  selection over double click, which keeps centring/unfolding) opens the
  symbol's file in a docked Source panel, tinted at its lines and scrolled
  there. Groups without nodes (clusters, directories) leave it unchanged.
- `SourceHighlight` (core): own C++ lexer per line with carried state
  (block comments, raw strings with delimiters), literal prefixes, digit
  separators and exponents, preprocessor directives with `<header>` names;
  `highlight_source` expands tabs and splits `\n`/`\r\n`; `resolve_source`
  tries roots in order. Tests in `test_source_highlight.cpp`.
- Layout: existing `ui.ini` files have no Source window; docking it via the
  Log's node failed (a hidden Log's node is pruned) and DockBuilder edits
  after the dock space's submission do not apply, so the panel is docked
  into a split of the graph area before submission, retrying until the
  layout exists, and is first drawn one frame later so it is never created
  floating.
- Smoke test: selects the first symbol with lines and checks the panel shows
  its file (35 and 21 lines for the fixture charts).
- Mistake on the way: one test run started in the repository and saved a
  floating Source window into the user's `.comprendo/ui.ini`; that entry was
  removed and later runs used a scratch directory with a copy of the layout.

- Commit: `1215ba4`.

## 2026-11-23 — M12f follow-up: the Source panel never opens itself

- Clarified by the user: selecting must not open the Source panel; the panel
  opens only from View → Source and must already show the current selection
  then. `update_source` already ran for every selection; the auto-open and
  the "Open source on selection" option (and its setting) are removed. The
  pending scroll to the symbol is kept until the panel is drawn.
- Smoke test: the selection loads the file while the panel stays closed
  (frame 29), then the panel is opened (frame 31). Checked under xvfb:
  select `compute_layout` with the panel closed, open View → Source, and it
  shows Layout.cpp at lines 598–686.

- Commit: `6b3bf32`.

## 2026-11-23 — M12g fold docked panels with ▼

- Question: can panels be tucked away to their tab? Not natively: ImGui
  collapses only floating windows; in a dock node the collapse button is the
  window menu (`imgui.cpp`: "the Collapse Button turns into the Window Menu
  button"). The user chose ▼ as the fold button.
- `Ui::fold_menu_handler` replaces `DockNodeWindowMenuHandler`; the request
  is applied at the next frame's start (DockBuilder-style edits must precede
  the dock space): size along the parent split axis = tab bar (or a 3-tab
  strip for side-by-side nodes), `WantLockSizeOnce` so the split takes it
  exactly. Unfold restores the size before folding (kept per node id for the
  session) or 35% of the parent. First attempt stopped at 33 px because
  docked windows obey `style.WindowMinSize` (32 px), so the second click did
  not recognise the fold; the minimum height now equals the tab bar.
- Tests: `fold_toggle_size`/`is_folded`; smoke test folds and unfolds the
  Explorer (default layout: side-by-side strip). Checked with XTest on a
  copy of the user's layout: fold, unfold, fold across a restart.

- Commit: `622db4c`.

## 2026-11-23 — M12h side panels fold into strips

- Request: panels on the left or right screen edge should fold into thin
  strips at the top corner, arranged like the panels (stacked → stacked,
  side by side → side by side). Implemented IDE-style: folded side panels
  are not drawn (ImGui keeps their dock nodes and hands the space to the
  graph; drawing them again restores place and size), and a strip with a
  vertical label stands in for each.
- Arrangement from the dock tree: positions at fold time were wrong once a
  neighbour had folded (the remaining node moves over), so each node's
  nominal rectangle is replayed from the splits' `SizeRef` ratios.
- Strip labels: `AddText` at the strip position was clipped away before the
  rotation; the text is laid out under a full-screen clip rectangle at the
  viewport corner and then rotated into the strip.
- A panel folded into a strip is no longer in its node's window list, so
  `toggle_fold` (and strip clicks) unfold it directly by name.
- Checked under xvfb with hand-written layouts: stacked left pair → two
  stacked strips top-left; side-by-side left pair → two strips side by side
  top-left; right Inspector → strip top-right reading downward, unfolded by a
  click to its former width. The user's own layout file was not touched.

- Commit: `57ed108`.

## 2026-11-23 — M12i animated focus from the panels

- Question: is the move to a panel selection animated? It was not:
  `apply_requests` fitted the camera at once (auto-fit), while only graph
  double clicks and Backspace used `CameraAnimation`.
- Now a focus request computes the fitted pose on a camera copy and glides
  there with `animate_to` (0.25 s, ease-out, zoom interpolated
  geometrically). Requests before the graph area is known keep the old
  immediate auto-fit, so `--select` still frames the selection at startup.
- Checked under xvfb: after an Explorer click the status zoom read 0.162,
  0.226, 0.273, 0.311, 0.344, 0.373 in successive captures over 0.14 s.

- Commit: `23f8905`.

## 2026-11-23 — Source panel: lighter selection tint

- The tint behind the selected symbol's lines (28% blue-grey) made the code
  hard to read; it is now 8% (12% was still too strong), and a 3 px bar in
  the gutter marks the range.

- Commit: `7251068`.

## 2026-11-23 — M12j C re-centres the layout on the selection

- Discussed two readings (re-centred nested view vs. neighbourhood view);
  the user chose the nested view.
- Layout engine: `Body::pinned` (repulsion is mass-weighted, so pinning by a
  huge mass would have pushed everything away); `layout_group` takes a pinned
  child and centres the group on it; `centre_layout` re-runs only the target's
  ancestors from the base layout; the top-down assembly is shared
  (`assemble_layout`); `blend_layouts` for the animation. Golden files
  unchanged.
- View/viewer: `ChartView::centre_on_group`/`centre_on_key` (kept by key across
  regrouping and reloads), `show_layout` for animation frames; `C` and View
  menu items; status shows "centred on X". C, like Backspace, works while a
  panel has keyboard focus (found under xvfb: after `--select` the Explorer
  held the keyboard and C was ignored).
- Tests: target exactly at the origin, invariants, off-path groups unchanged,
  determinism, blend ends; centring survives regrouping; smoke test centres
  and restores. Measured on this chart (release): 0.8 ms for a top-level
  cluster, 0.9 ms for a function four levels deep.
