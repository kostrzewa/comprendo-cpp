# AGENTS.md

Operating notes for agents working in this repository.

## Project

Comprendo is a code explorer and visualizer: a C++ core/viewer/CLI plus a
TypeScript opencode plugin. Read these before changing anything:

- `plan/comprendo-cpp.md` — requirements (source of truth)
- `plan/architecture.md` — components, targets, extractor design
- `plan/chart-schema.md` — HDF5 layout and invariants
- `plan/milestones.md` — current milestone and acceptance criteria

## Toolchain

Always use the versioned clang-22 tools, preferring 22. Never invoke the
unversioned binaries.

- `clang-22`, `clang++-22` — compiler
- `clangd-22` — semantic extractor backend
- `clang-format-22` — formatting
- `clang-tidy-22` — lint
- `llvm-cov-22`, `llvm-profdata-22` — coverage

Build with CMake + Ninja through the presets:

```sh
cmake --preset clang-22
cmake --build --preset clang-22 -j"$(nproc)"
ctest --preset clang-22 --output-on-failure
```

Use `clang-22-release` for optimized builds. `compile_commands.json` lives in
the build directory; pass `--compile-commands-dir=<build>` to `clangd-22`.

## Packages and environments

- Do not install packages or create side environments (conda, nix, etc.).
  Interrupt and ask the user to install system packages.
- Python virtual environments are allowed when needed.
- C++ dependencies come from `FetchContent` into `build/`; do not vendor them.
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

Doxygen documentation is non-negotiable. Build it with:

```sh
cmake --build --preset clang-22 --target doxygen
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
- Add tests with every feature and run `ctest --preset clang-22` before
  finishing.
- Gate tests that need external tools (for example `clangd-22`) on their
  presence.

## Plan maintenance

`plan/` must stay current (non-negotiable). When scope changes, update the
relevant document and the milestone status in `plan/milestones.md`.

## Commits

Make small, frequent commits. Do not commit unless the user asks. Run the build
and tests before committing.

## Layout

- `include/` — public headers (`Comprendo/`, `Logger/`, `Profiler/`)
- `src/` — viewer and CLI entry points and implementations
- `tests/` — Catch2 tests
- `plugin/` — TypeScript opencode plugin
- `cmake/` — dependency, doxygen, and git-hash setup
- `docs/` — Doxygen assets and main page
- `plan/` — requirements and implementation plans

Targets: `comprendo_core`, `comprendo`, `comprendo-cli`, `comprendo_tests`.

## Maintainability

Use the `cpp-maintainability` MCP server to scan changed C++ files before
finishing.
