/**
 * @file Tests for the chart tools, run against temporary projects.
 */

import { Database } from "bun:sqlite";
import { afterEach, beforeEach, describe, expect, test } from "bun:test";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { ChartError } from "../src/chart.ts";
import { formatId, fromSql, nodeId, Provenance } from "../src/ids.ts";
import { ComprendoPlugin } from "../src/index.ts";
import { createTools, runTool, type ToolArgs } from "../src/tools.ts";

/** Scratch project directory of the current test. */
let project = "";

beforeEach(() => {
  project = mkdtempSync(join(tmpdir(), "comprendo-plugin-"));
});

afterEach(() => {
  rmSync(project, { recursive: true, force: true });
});

/**
 * Run a tool in the scratch project.
 *
 * @param name Tool name.
 * @param args Arguments.
 * @returns Tool output.
 */
function call(name: string, args: ToolArgs = {}): string {
  return runTool(createTools(project), name, args, project);
}

/**
 * Open the scratch project's chart read-only with ids as BigInt.
 *
 * @returns Connection; close it after use.
 */
function inspect(): Database {
  return new Database(join(project, ".comprendo/chart.sqlite"), { readonly: true, safeIntegers: true });
}

/** Id of the node `geo::area` added by {@link addArea}. */
const AREA_ID = nodeId(8, "geo::area", "src/geo.cpp", "(double)");

/**
 * Add `geo::area` in `src/geo.cpp`.
 *
 * @returns Tool output.
 */
function addArea(): string {
  return call("chart_add_node", {
    kind: "function",
    qualified_name: "geo::area",
    file: "src/geo.cpp",
    line_start: 3,
    line_end: 7,
    signature: "(double)",
    summary: "Area of a disk",
    detail: "",
    parent: "",
  });
}

describe("chart_add_node", () => {
  // Without the CLI the model builds the chart itself, so adding a symbol must
  // also create its file and the Contains edge the viewer's tree needs, with
  // the same id the C++ extractor would compute. Adds a function, then checks
  // the rows, the provenance, and that adding it again reports the existing node.
  test("adds the node under its file with a C++-compatible id", () => {
    expect(addArea()).toContain(formatId(AREA_ID));
    const db = inspect();
    const rows = db.query("SELECT id, kind, name, provenance FROM nodes ORDER BY kind").all() as {
      id: bigint;
      kind: bigint;
      name: string;
      provenance: bigint;
    }[];
    expect(rows.map((row) => [Number(row.kind), row.name])).toEqual([
      [1, "geo.cpp"],
      [8, "area"],
    ]);
    expect(fromSql(rows[1]!.id)).toBe(AREA_ID);
    expect(rows.every((row) => Number(row.provenance) === Provenance.Model)).toBe(true);
    const edge = db.query("SELECT src, dst, kind FROM edges").get() as { src: bigint; dst: bigint; kind: bigint };
    expect(fromSql(edge.src)).toBe(nodeId(1, "src/geo.cpp", "src/geo.cpp", ""));
    expect(fromSql(edge.dst)).toBe(AREA_ID);
    expect(Number(edge.kind)).toBe(1);
    db.close();
    expect(addArea()).toContain("already in the chart");
  });

  // Models make mistakes in arguments; each must come back as a ChartError the
  // model can read and fix, and nothing may be written. Calls the tool with an
  // unknown kind, a path outside the project, a missing name, and a parent
  // that does not exist, then checks the chart is still empty.
  test("rejects bad arguments without writing", () => {
    const base = { kind: "function", qualified_name: "f", file: "a.cpp" };
    expect(() => call("chart_add_node", { ...base, kind: "lambda" })).toThrow(/unknown kind/);
    expect(() => call("chart_add_node", { ...base, file: "../a.cpp" })).toThrow(/outside the project/);
    expect(() => call("chart_add_node", { ...base, qualified_name: "" })).toThrow(/qualified_name is required/);
    expect(() => call("chart_add_node", { ...base, parent: "0x1" })).toThrow(ChartError);
    expect(() => call("chart_add_node", { ...base, line_start: -1 })).toThrow(/non-negative/);
    const db = inspect();
    expect(Number(db.query("SELECT count(*) AS n FROM nodes").get()!.n as bigint)).toBe(0);
    db.close();
  });
});

describe("chart_find_node and chart_annotate_node", () => {
  // Annotating is the main job of /chart: the model finds a node, then writes
  // its explanation. Adds a node, finds it by short name and by file, annotates
  // the detail only, and checks the summary is kept, the detail written, and
  // that an empty annotation is refused.
  test("find a node and annotate it", () => {
    addArea();
    expect(call("chart_find_node", { query: "area", file: "", kind: "", limit: 0 })).toContain("geo::area (double)");
    expect(call("chart_find_node", { query: "", file: "src", kind: "file", limit: 5 })).toContain("src/geo.cpp");
    expect(call("chart_find_node", { query: "nothing", file: "", kind: "", limit: 5 })).toBe("no matching nodes");
    call("chart_annotate_node", { id: formatId(AREA_ID), summary: "", detail: "Uses pi * r^2." });
    const db = inspect();
    expect(db.query("SELECT summary, detail FROM nodes WHERE kind = 8").get()).toEqual({
      summary: "Area of a disk",
      detail: "Uses pi * r^2.",
    });
    db.close();
    expect(() => call("chart_annotate_node", { id: formatId(AREA_ID), summary: "", detail: "" })).toThrow(
      /summary, a detail/,
    );
  });
});

describe("chart_annotate_cluster", () => {
  // Clusters are addressed by label so the model can refer to them without
  // ids; nesting must stay acyclic, and one bad member must not leave a half
  // written cluster. Creates a parent and a child with a member, checks the
  // rows, then tries a cycle, an unknown parent, and an unknown member.
  test("creates nested clusters and assigns members atomically", () => {
    addArea();
    call("chart_annotate_cluster", { label: "Geometry", summary: "Shapes", color: "", parent_label: "", members: [] });
    const output = call("chart_annotate_cluster", {
      label: "Areas",
      summary: "Area formulas",
      color: "#336699",
      parent_label: "Geometry",
      members: [formatId(AREA_ID)],
    });
    expect(output).toContain("assigned 1 nodes");
    expect(() =>
      call("chart_annotate_cluster", { label: "Geometry", summary: "", color: "", parent_label: "Areas", members: [] }),
    ).toThrow(/cycle/);
    expect(() =>
      call("chart_annotate_cluster", { label: "X", summary: "", color: "", parent_label: "Nope", members: [] }),
    ).toThrow(/no cluster labelled/);
    expect(() =>
      call("chart_annotate_cluster", { label: "Y", summary: "", color: "", parent_label: "", members: ["0x1"] }),
    ).toThrow(/no node/);
    expect(() =>
      call("chart_annotate_cluster", { label: "Z", summary: "", color: "teal", parent_label: "", members: [] }),
    ).toThrow(/#4a90d9/);

    const db = inspect();
    const clusters = db.query("SELECT label, color, parent_id IS NOT NULL AS nested FROM clusters ORDER BY label").all();
    expect(clusters).toEqual([
      { label: "Areas", color: 0x336699n, nested: 1n },
      { label: "Geometry", color: expect.any(BigInt), nested: 0n },
    ]);
    expect(db.query("SELECT count(*) AS n FROM nodes WHERE cluster_id IS NOT NULL").get()).toEqual({ n: 1n });
    db.close();
  });
});

describe("chart_add_edge and chart_finish", () => {
  // Edges written by the model must merge with extractor edges instead of
  // failing on the (src, dst, kind) key, and chart_finish must leave a chart
  // the viewer can open. Adds two nodes, an edge twice (second without
  // evidence), checks the merged row, then finishes and checks the meta stamp
  // and the printed viewer command.
  test("upserts edges and stamps the finished chart", () => {
    addArea();
    call("chart_add_node", { kind: "function", qualified_name: "main", file: "src/main.cpp", signature: "()" });
    const mainId = nodeId(8, "main", "src/main.cpp", "()");
    const edge = { src: formatId(mainId), dst: formatId(AREA_ID), kind: "calls" };
    expect(call("chart_add_edge", { ...edge, weight: 2, evidence: "src/main.cpp:4" })).toStartWith("added");
    expect(call("chart_add_edge", { ...edge, weight: 3, evidence: "" })).toStartWith("updated");
    expect(() => call("chart_add_edge", { ...edge, dst: edge.src })).toThrow(/two different/);
    expect(() => call("chart_add_edge", { ...edge, kind: "owns" })).toThrow(/unknown edge kind/);

    const finished = call("chart_finish");
    expect(finished).toContain(`comprendo ${join(project, ".comprendo/chart.sqlite")}`);
    expect(finished).toContain("1 of 2 symbols annotated");

    const db = inspect();
    expect(db.query("SELECT weight, evidence FROM edges WHERE kind = 3").get()).toEqual({
      weight: 3,
      evidence: "src/main.cpp:4",
    });
    const meta = Object.fromEntries(
      (db.query("SELECT key, value FROM meta").all() as { key: string; value: string }[]).map((row) => [row.key, row.value]),
    );
    expect(meta.annotator).toStartWith("comprendo-opencode");
    expect(meta.generator).toStartWith("comprendo-opencode");
    db.close();
  });
});

describe("ComprendoPlugin", () => {
  // opencode wires the plugin through its config hook and tool map; /chart
  // must appear without overriding a user's own /chart, and tool calls must
  // reach the session's directory. Creates the hooks, applies the config hook
  // to an empty and a customized config, and runs one tool through execute().
  test("registers /chart and the chart tools", async () => {
    const hooks = await ComprendoPlugin({ directory: "/nonexistent" });
    const config: { command?: Record<string, { template: string; description: string }> } = {};
    await hooks.config(config);
    expect(config.command?.chart?.template).toContain("chart_finish");
    const custom = { command: { chart: { template: "mine", description: "mine" } } };
    await hooks.config(custom);
    expect(custom.command.chart.template).toBe("mine");

    expect(Object.keys(hooks.tool).sort()).toEqual([
      "chart_add_edge",
      "chart_add_node",
      "chart_annotate_cluster",
      "chart_annotate_node",
      "chart_find_node",
      "chart_finish",
      "chart_overview",
    ]);
    for (const tool of Object.values(hooks.tool)) {
      expect(tool.description.length).toBeGreaterThan(20);
    }
    const overview = await hooks.tool.chart_overview!.execute({}, { directory: project });
    expect(overview).toContain(join(project, ".comprendo/chart.sqlite"));
    expect(overview).toContain("comprendo-cli extract");
  });
});
