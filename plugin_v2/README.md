# comprendo-opencode-v2

Plugin for opencode 2.x (the v2 plugin API): adds `/chart`, which has the
model explore the project and write a Comprendo chart
(`.comprendo/chart.sqlite`), and the seven `chart_*` tools it uses. For
opencode 1.x use `../plugin` instead.

## Install

Load the directory by path in `opencode.json` (2.x reads `plugins`; configured
local paths must be directories):

```json
{
  "plugins": ["/path/to/comprendo-cpp/plugin_v2"]
}
```

The plugin shares its chart code, `/chart` prompt, and embedded schema with
the 1.x plugin (`../plugin/src`), so keep both directories together. It has no
dependencies besides Bun's built-ins (`bun:sqlite`).

## How it maps onto the v2 API

- `index.ts` default-exports `{ id: "comprendo", setup }`, the shape
  `Plugin.define` returns.
- `setup` registers the tools with `ctx.tool.transform(editor => editor.add(...))`
  (JSON Schema inputs, `execute` returning `{ content }`) and `/chart` with
  `ctx.command.transform`. Plugin commands in 2.x are code, not templates:
  `/chart`'s `execute({ sessionID, prompt, delivery })` sends the filled-in
  template to the session with `ctx.session.prompt`.
- opencode 2.x runs plugin tools through Code Mode by default: the model calls
  them from a script (`await tools.chart_overview()`), which lets it annotate
  many nodes in one step.
- The project directory comes from `ctx.location.directory`.
- Argument mistakes come back as `error: ...` tool output for the model to fix.

## Test

```sh
bun test                         # or: BUN_BE_BUN=1 opencode test
```

Verified with opencode 2.2.7 (`--standalone`, scratch project): the plugin
loads, a model call to `chart_overview` creates the project's chart, and
`/chart` invoked through the session command API runs its `execute`. The
unit tests use a fake v2 context; in opencode 1.19.5's v2 preview `/chart`
registers as a template and tools are skipped (no tool registry there).
