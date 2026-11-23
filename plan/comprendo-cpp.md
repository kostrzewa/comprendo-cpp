Build a code explorer and visualizer as an opencode plugin which uses the model in opencode to explore and understand the codebase.

The system is based on two parts:

- an explorer / visualizer written using raylib which can be used to navigate a codebase visually including the functionality explanations produced by the model
- an opencode plugin which provides a /chart command to opencode which produces some kind of output file which can later be opened with the explorer / visualizer

Since M11 `/chart` is also available in Claude Code (`claude_plugin/`), and the chart tools of all assistants are served by `comprendo-cli mcp`.

Technologies to be used:

-  CMake as a build system, GCC as a compiler, external libraries to be included via FetchContent_Declare FetchContent_MakeAvailable if possible
-  https://github.com/raysan5/raylib/ as a base library
-  https://github.com/SFML/SFML for anything basic not supported by raylib (sound, for example)
-  https://github.com/jrouwe/JoltPhysics as a physics engine
-  https://github.com/fmtlib/fmt for formatted output
-  https://github.com/gabime/spdlog for logging
-  https://github.com/ph3at/libenvpp for handling environment variables
-  https://github.com/ToruNiina/toml11.git for textual input files
-  https://github.com/CLIUtils/CLI11 for command-line argument parsing (`comprendo-cli` and the viewer)
-  SQLite (amalgamation via FetchContent) as the chart store written by the C++ tools (`comprendo-cli extract` and the chart tools behind `/chart`)
-  include/Profiler/Profiler.hpp for profiling (output to perfetto traces)
-  include/Logger/Logger.hpp (for logging, uses spdlog) from which you should remove the depdendency on MPI and Kronos
-  absolutely non-negiotable: doxygen documentation of all functions, classes, class members, class methods etc., see docs/doxygen and cmake/doxygen_setup.cmake
-  logs should always contain the git commit at startup, see cmake/git_hash.cmake 

You should implement algorithms yourself.

Assets should all be vector graphics that you produce yourself or creative-commons-licensed assets, sounds should be creative-commons licensed or DSP-generated.

You are expected to write deterministic tests whenever possible.

Keep an implementation plan in the plan/ directory, you can structure this however you want and cross-reference files. It's important that you keep it up-to date (NON-NEGIOTABLE).

Small, frequent commits. Run tests regularly.

Use the cpp-maintainability MCP server to check if you are writing maintainable code.

Use the anti-slop skill to prevent slop.

Use the cpp-coding-standards skill to check if you are following CPP coding standards.

Use clang-format-22 to format code, format given by .clang-format

## Implementation plans

- [architecture.md](architecture.md) — components, targets, extractor design, toolchain convention.
- [chart-schema.md](chart-schema.md) — SQLite schema, node/edge kinds, ids, validation.
- [milestones.md](milestones.md) — M0–M7 with acceptance criteria and test gates.
- [log.md](log.md) — progress log: one entry per milestone with decisions and commits.
- [`schema/chart.sql`](../schema/chart.sql) — the chart DDL.
