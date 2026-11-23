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
  `ctx.command.transform`.
- The project directory comes from `ctx.location.directory`.
- Argument mistakes come back as `error: ...` tool output for the model to fix.

## Test

```sh
bun test                         # or: BUN_BE_BUN=1 opencode test
```

Verified so far: the unit tests (fake v2 context) and loading in the v2
runtime built into opencode 1.19.5, where `/chart` registers (that preview
has no tool registry, so tools are skipped there). Not yet run against an
opencode 2.x install.
