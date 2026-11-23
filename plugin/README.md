# comprendo-opencode

opencode plugin adding `/chart`: the model explores the project and writes a
Comprendo chart (`.comprendo/chart.sqlite`) that the `comprendo` viewer opens.

## Install

The plugin has no dependencies besides Bun's built-ins and reads the shared
schema from `../schema/chart.sql`, so load it from this repository by path. In
the project's `opencode.json` (or `~/.config/opencode/opencode.json` for all
projects):

```json
{
  "plugin": ["/path/to/comprendo-cpp/plugin/src/index.ts"]
}
```

Put `comprendo-cli` on `PATH` so `/chart` can start from the extracted
structure (with clangd-22 call edges when a `compile_commands.json` exists);
without it the model builds the chart from scratch.

## Use

```
/chart                      chart the whole project
/chart focus on src/net     extra instructions are passed to the model
```

The session ends with `chart_finish`, which prints the viewer command, e.g.
`comprendo /project/.comprendo/chart.sqlite`.

## Tools

| Tool | Purpose |
| --- | --- |
| `chart_overview` | Counts, clusters, and per-file annotation progress |
| `chart_find_node` | Look up nodes and their `0x…` ids |
| `chart_add_node` | Add a symbol the extractor missed (placed under its file) |
| `chart_annotate_node` | Set a node's summary and detail |
| `chart_annotate_cluster` | Create or update a cluster by label and assign nodes |
| `chart_add_edge` | Add or update a relationship |
| `chart_finish` | Check integrity, stamp the chart, print the viewer command |

Every call opens the chart, runs one transaction, and closes it, so
`comprendo-cli extract` and the viewer can use the same file in between.

## Test

```sh
bun test                         # or, with only opencode installed:
BUN_BE_BUN=1 opencode test
```

CTest runs these tests (`plugin_tests`) and a cross-check that annotates a
CLI-extracted chart and validates it with `comprendo-cli validate`.
