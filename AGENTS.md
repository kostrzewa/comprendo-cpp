# AGENTS.md

Operating notes for agents working in this repository.

## Project

Comprendo is a code explorer and visualizer: a C++ core/viewer/CLI (which also
serves the `/chart` tools over MCP) plus thin plugins for Claude Code and
opencode. Read these before changing anything:

- `plan/comprendo-cpp.md` — requirements (source of truth)
- `plan/architecture.md` — components, targets, extractor design
- `plan/chart-schema.md` — SQLite schema and invariants (DDL in `schema/chart.sql`)
- `plan/milestones.md` — current milestone and acceptance criteria

## Toolchain

Always use the versioned clang-22 tools, preferring 22. Never invoke the
unversioned binaries.

- `clang-22`, `clang++-22` — compiler
- `clangd-22` — semantic extractor backend
- `clang-format-22` — formatting
- `clang-tidy-22` — lint: `cmake --build --preset clang-22 --target clang-tidy`
- `llvm-cov-22`, `llvm-profdata-22` — coverage

Build with CMake + Ninja through the presets:

```sh
cmake --preset clang-22
cmake --build --preset clang-22 -j"$(nproc)"
ctest --preset clang-22 --output-on-failure
```

Use `clang-22-release` for optimized builds; it defaults `CMAKE_C_FLAGS` and
`CMAKE_CXX_FLAGS` to `-march=native -mtune=native` (override them for portable
binaries). `comprendo_core` is built with `-ffp-contract=off` so layouts are
bit-identical across builds and CPUs; keep it. `compile_commands.json` lives in
the build directory; pass `--compile-commands-dir=<build>` to `clangd-22`.

UI: Dear ImGui `v1.92.7-docking` + rlImGui `Raylib_5_5`. The graph is the
transparent central dock node; do not replace the docking shell with fixed
panels without updating the plan.

## Packages and environments

- Do not install packages or create side environments (conda, nix, etc.).
  Interrupt and ask the user to install system packages.
- Python virtual environments are allowed when needed.
- C++ dependencies come from `FetchContent` into `build/`; do not vendor them.
  Acknowledge every new dependency in `THIRD_PARTY_NOTICES.md` (table row and
  license text under a `<!-- fetchcontent: <name> -->` marker); CTest
  `third_party_notices` fails otherwise.
- Put temporary files under `/tmp/<harness>/<project-name>`, e.g.
  `/tmp/opencode/comprendo`.

## Code style

- Format with `clang-format-22` using the repository `.clang-format`.
- C++23, 2-space indent, Allman braces, return type on its own line, 100 columns.
- Namespace is `Comprendo`; macros and CMake options use the `COMPRENDO_` prefix.
- Do not add comments that restate the code. Doxygen comments are mandatory for
  every file, function, class, member, and method.
- Follow the C++ Core Guidelines (`cpp-coding-standards` skill) and run the
  `anti-slop` skill before and after writing code.

## Documentation

Doxygen documentation is non-negotiable. The target exists only with
`COMPRENDO_BUILD_DOXYGEN=ON`; it warns about every undocumented member,
parameter, and return value and must stay warning-free:

```sh
cmake --preset clang-22 -B build/doxygen -DCOMPRENDO_BUILD_DOXYGEN=ON
cmake --build build/doxygen --target doxygen
```

Keep `docs/main.md` and the `plan/` documents current.

## Logging and profiling

- Use `Comprendo::Logger` (spdlog). Startup logs must contain the project
  version and git commit.
- Wrap performance-sensitive work in `Profiler` scopes and dump Perfetto traces
  when investigating performance.

## Testing

- Use Catch2 and CTest. Prefer deterministic tests; use golden files for layout
  and serialization.
- The opencode plugins use `bun test` (`opencode_plugin/test/`, `opencode_v2_plugin/test/`). Bun
  is not required: opencode is a Bun executable, so `BUN_BE_BUN=1 opencode
  test` in `opencode_plugin/` or `opencode_v2_plugin/` runs them. The chart tools themselves are
  C++ and tested with Catch2 (`test_chart_tools.cpp`, `test_mcp.cpp`).
- Add tests with every feature and run `ctest --preset clang-22` before
  finishing.
- Gate tests that need external tools (for example `clangd-22`) on their
  presence.
- Use CHECK instead of REQUIRE so tests run even if some test cases fail.
  Never dereference a value a CHECK may have rejected: compare without
  dereferencing where possible (`CHECK(result == expected)` works on
  `std::expected` and `std::optional`), otherwise follow the CHECK with an
  early `return` guard, e.g. `CHECK(node != nullptr); if (node == nullptr)
  { return; }`. A crash in one test case aborts the whole test binary.
- Every test case should be prefaced with an approximately 5 line comment
  which explains why the test is there, what it does and how it does it. The
  comment must be kept up-to-date.

### Window tests

Never run the viewer against the user's desktop display. Use a virtual
framebuffer so no window appears on their desktop:

```sh
xvfb-run -a ./build/clang-22-debug/comprendo
```

If `xvfb-run` is unavailable, ask the user to install `xvfb`; do not fall back
to the interactive `DISPLAY`. For in-process checks, prefer a hidden raylib
window (`FLAG_WINDOW_HIDDEN`) and exit after a fixed number of frames.

The viewer's `--smoke-test` flag does exactly that: it renders a hidden window
for a fixed number of frames and exercises resize and fullscreen. CTest
registers it as `viewer_smoke` when `xvfb-run` is available.

The viewer starts hidden and is shown after initialization so tiling window
managers can place it. Do not reintroduce `FLAG_WINDOW_HIGHDPI` without fixing
raylib's resize callback.

## Plan maintenance

`plan/` must stay current (non-negotiable). When scope changes, update the
relevant document and the milestone status in `plan/milestones.md`.

## Commits

- Make small, frequent commits. Do not commit unless the user asks. Run the build
and tests before committing.
- No agent attribution in commit messages!

## Layout

- `include/` — public headers (`Comprendo/`, `Logger/`, `Profiler/`)
- `src/` — viewer and CLI entry points and implementations
- `tests/` — Catch2 tests
- `claude_plugin/` — Claude Code plugin (`/comprendo:chart` skill, MCP entry);
  the repository root's `.claude-plugin/marketplace.json` lists it
- `opencode_plugin/` — opencode 1.x plugin (`/chart`, registers `comprendo-cli mcp`)
- `opencode_v2_plugin/` — opencode 2.x plugin, an adapter over `opencode_plugin/src`
- `prompts/` — `chart.md`, the `/chart` prompt of all assistants; after
  editing it, run `cmake -DMODE=write -DPROMPT=prompts/chart.md
  -DSKILL=claude_plugin/skills/chart/SKILL.md -P cmake/sync_prompt.cmake` to
  refresh the Claude skill's copy (CTest `chart_prompt_in_sync` checks it)
- `schema/` — `chart.sql`, the SQLite DDL embedded into the C++ tools
- `cmake/` — dependency, doxygen, and git-hash setup
- `docs/` — Doxygen assets and main page
- `plan/` — requirements and implementation plans

Targets: `comprendo_core`, `comprendo`, `comprendo-cli`, `comprendo_tests`.

## Maintainability

Use the `cpp-maintainability` MCP server to scan changed C++ files before
finishing.
