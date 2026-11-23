# comprendo-opencode

opencode 1.x plugin adding `/chart`: the model explores the project and builds
a Comprendo chart (`.comprendo/chart.sqlite`) that the `comprendo` viewer
opens.

The chart tools are not part of the plugin: they are served by
`comprendo-cli mcp`, which the plugin registers as the MCP server `comprendo`
(Claude Code and the opencode 2.x plugin in `../opencode_v2_plugin` use the same
server). The plugin itself only adds the command and the server to opencode's
configuration, leaving a user-defined `/chart` or `comprendo` server alone.

## Install

Put `comprendo-cli` on `PATH`, or set `COMPRENDO_CLI` to its path. Then load
the plugin by path in the project's `opencode.json` (or
`~/.config/opencode/opencode.json` for all projects):

```json
{
  "plugin": ["/path/to/comprendo-cpp/opencode_plugin/src/index.ts"]
}
```

`opencode mcp list` shows the `comprendo` server as connected.

## Use

```
/chart                      chart the whole project
/chart focus on src/net     extra instructions are passed to the model
```

The session ends with `chart_finish`, which prints the viewer command, e.g.
`comprendo /project/.comprendo/chart.sqlite`.

## Files

- `src/index.ts` — plugin entry (`config` hook)
- `src/prompt.ts` — `/chart`, reading the shared prompt `../prompts/chart.md`
- `src/server.ts` — the MCP server entry (`comprendo-cli mcp --root <project>`)

Tests: `bun test`, or `BUN_BE_BUN=1 opencode test` without a Bun install.
