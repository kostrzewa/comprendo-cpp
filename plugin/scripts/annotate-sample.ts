/**
 * @file Annotate a chart the way a /chart session would, for the CTest cross-check.
 *
 * Usage: `bun scripts/annotate-sample.ts <source-chart> <project-dir>`. The
 * source chart, written by `comprendo-cli extract` on
 * `tests/fixtures/extract_basic`, is copied to
 * `<project-dir>/.comprendo/chart.sqlite` and annotated there. Exits non-zero
 * when any tool call fails.
 */

import { copyFileSync, mkdirSync, rmSync } from "node:fs";
import { join } from "node:path";

import { DEFAULT_CHART_PATH } from "../src/chart.ts";
import { createTools, runTool, type ToolArgs } from "../src/tools.ts";

const [source, project] = process.argv.slice(2);
if (source === undefined || project === undefined) {
  console.error("usage: annotate-sample.ts <source-chart> <project-dir>");
  process.exit(2);
}
const target = join(project, DEFAULT_CHART_PATH);
rmSync(join(project, ".comprendo"), { recursive: true, force: true });
mkdirSync(join(project, ".comprendo"), { recursive: true });
copyFileSync(source, target);
const tools = createTools(project);

/**
 * Run a tool and echo its output.
 *
 * @param name Tool name.
 * @param args Arguments.
 * @returns Tool output.
 */
function call(name: string, args: ToolArgs = {}): string {
  const output = runTool(tools, name, args, project!);
  console.log(`${name}: ${output.split("\n")[0]}`);
  return output;
}

/**
 * Find exactly one node id.
 *
 * @param query Qualified name.
 * @param file File to search in.
 * @returns Node id as printed by the tools.
 */
function findId(query: string, file: string): string {
  const found = call("chart_find_node", { query, file, kind: "", limit: 1 });
  const id = /^0x[0-9a-f]{16}/.exec(found)?.[0];
  if (id === undefined) {
    throw new Error(`${query} not found in ${file}`);
  }
  return id;
}

call("chart_overview");
const main = findId("main", "src/main.cpp");
const circle = findId("geo::Circle", "src/circle.hpp");
call("chart_annotate_node", { id: main, summary: "Entry point: scales a circle's area", detail: "" });
call("chart_annotate_node", { id: circle, summary: "A disk with a radius", detail: "Implements geo::Shape." });
call("chart_add_node", {
  kind: "macro",
  qualified_name: "GEO_EXPORT",
  file: "include/geo/shape.hpp",
  line_start: 1,
  line_end: 1,
  signature: "",
  summary: "Visibility macro (added by the model)",
  detail: "",
  parent: "",
});
call("chart_add_edge", { src: main, dst: circle, kind: "uses", weight: 1, evidence: "src/main.cpp:33" });
call("chart_annotate_cluster", { label: "Geometry", summary: "Shapes", color: "", parent_label: "", members: [circle] });
call("chart_annotate_cluster", { label: "Program", summary: "Entry", color: "#aa5500", parent_label: "Geometry", members: [main] });
call("chart_finish");
