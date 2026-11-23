/**
 * @file Tests that the plugin carries the chart schema and needs nothing outside `plugin/`.
 */

import { describe, expect, test } from "bun:test";
import { cpSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { renderSchemaModule } from "../scripts/embed-schema.ts";
import { CHART_SCHEMA } from "../src/schema.ts";

/** Plugin directory. */
const PLUGIN_DIR = new URL("..", import.meta.url).pathname;

describe("embedded schema", () => {
  // The plugin writes charts that the C++ tools read, so its embedded DDL must
  // be exactly schema/chart.sql; a stale copy would create charts the viewer
  // rejects or misreads. Reads the shared file, checks the constant equals it,
  // and checks src/schema.ts is byte-for-byte what embed-schema.ts generates
  // (otherwise: run `bun scripts/embed-schema.ts`).
  test("matches schema/chart.sql", () => {
    const sql = readFileSync(new URL("../../schema/chart.sql", import.meta.url), "utf8");
    expect(CHART_SCHEMA).toBe(sql);
    expect(readFileSync(new URL("../src/schema.ts", import.meta.url), "utf8")).toBe(renderSchemaModule(sql));
  });

  // The plugin runs on other repositories, possibly copied away from this one,
  // so it must not read files outside its own directory. Copies plugin/src
  // alone into a scratch directory, runs chart_overview and chart_add_node
  // from the copy against a fresh project, and checks the chart was created
  // with the C++ identity.
  test("works from a copy of plugin/src alone", async () => {
    const scratch = mkdtempSync(join(tmpdir(), "comprendo-plugin-copy-"));
    try {
      cpSync(join(PLUGIN_DIR, "src"), join(scratch, "plugin"), { recursive: true });
      const project = join(scratch, "project");
      const { default: plugin } = await import(join(scratch, "plugin", "index.ts"));
      const hooks = await plugin.server({ directory: project });
      await hooks.tool.chart_add_node.execute({ kind: "function", qualified_name: "f", file: "a.cpp" }, {});
      const overview: string = await hooks.tool.chart_overview.execute({}, {});
      expect(overview).toContain("function 1");
      const { Database } = await import("bun:sqlite");
      const db = new Database(join(project, ".comprendo/chart.sqlite"), { readonly: true });
      expect(db.query("PRAGMA application_id").values()[0]?.[0]).toBe(0x434d5052);
      db.close();
    } finally {
      rmSync(scratch, { recursive: true, force: true });
    }
  });
});
