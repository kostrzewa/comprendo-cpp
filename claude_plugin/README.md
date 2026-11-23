# Comprendo for Claude Code

`/comprendo:chart` has Claude explore the project and build a Comprendo chart
(`.comprendo/chart.sqlite`), which the `comprendo` viewer opens.

## Requirements

`comprendo-cli` from this repository, on `PATH` or named by `COMPRENDO_CLI`
(for example `export COMPRENDO_CLI=/path/to/comprendo-cpp/build/clang-22-release/comprendo-cli`).
It extracts the project's structure and serves the chart tools
(`comprendo-cli mcp`) the skill uses.

## Install

From the repository's marketplace (persistent):

```sh
claude plugin marketplace add /path/to/comprendo-cpp
claude plugin install comprendo@comprendo
```

For one session only:

```sh
claude --plugin-dir /path/to/comprendo-cpp/claude_plugin
```

`/mcp` lists the server as `plugin:comprendo:chart`.

## Use

```
/comprendo:chart                    chart the whole project
/comprendo:chart focus on src/net   extra instructions are passed to the model
```

The run ends with `chart_finish`, which prints the viewer command, e.g.
`comprendo /project/.comprendo/chart.sqlite`. The skill only runs when you
invoke it; Claude does not start it on its own.

## Contents

- `.claude-plugin/plugin.json` — plugin manifest
- `.mcp.json` — the `chart` MCP server: `comprendo-cli mcp --root <project>`
- `skills/chart/SKILL.md` — the `/chart` prompt; its body is a copy of
  `../prompts/chart.md`, kept in sync by `cmake/sync_prompt.cmake`
