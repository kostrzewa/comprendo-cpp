# comprendo-opencode-v2

Plugin for opencode 2.x (the v2 plugin API): adds `/chart`, which has the
model explore the project and build a Comprendo chart
(`.comprendo/chart.sqlite`), and registers the MCP server `comprendo`
(`comprendo-cli mcp`) that provides the seven `chart_*` tools. For opencode
1.x use `../opencode_plugin` instead; Claude Code uses `../claude_plugin` and the same
server.

## Install

Put `comprendo-cli` on `PATH`, or set `COMPRENDO_CLI` to its path. Load the
directory by path in `opencode.json` (2.x reads `plugins`; configured local
paths must be directories):

```json
{
  "plugins": ["/path/to/comprendo-cpp/opencode_v2_plugin"]
}
```

The plugin imports the `/chart` command and the server entry from
`../opencode_plugin/src` (which reads `../prompts/chart.md`), so keep the repository
together. It has no dependencies besides Bun's built-ins.

## How it maps onto the v2 API

- `index.ts` default-exports `{ id: "comprendo", setup }`, the shape
  `Plugin.define` returns.
- `setup` registers the server with `ctx.mcp.transform(editor =>
  editor.set("comprendo", { type: "local", command: [...] }))` unless one of
  that name is configured, and `/chart` with `ctx.command.transform`. Plugin
  commands in 2.x are code, not templates: `/chart`'s
  `execute({ sessionID, prompt, delivery })` sends the filled-in template to
  the session with `ctx.session.prompt`.
- opencode 2.x runs MCP tools through Code Mode by default: the model calls
  them from a script (`await tools.comprendo.chart_overview({})`), which lets
  it annotate many nodes in one step.
- The project directory comes from `ctx.location.directory`.

## Test

```sh
bun test                         # or: BUN_BE_BUN=1 opencode test
```

Verified with opencode 2.2.7 (`--standalone`, scratch project): a session's
Code Mode call to `tools.comprendo.chart_overview` reached `comprendo-cli mcp`
and created the project's chart. The MCP transform runs lazily, when opencode
builds its MCP registry, not during `setup`.
