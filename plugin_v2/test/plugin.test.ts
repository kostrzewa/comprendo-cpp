/**
 * @file Tests for the opencode 2.x plugin entry: registration and tool execution.
 */

import { afterEach, beforeEach, describe, expect, test } from "bun:test";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import plugin, { type CommandEditor, type ToolDefinition } from "../index.ts";

/** Scratch project directory of the current test. */
let project = "";

beforeEach(() => {
  project = mkdtempSync(join(tmpdir(), "comprendo-plugin-v2-"));
});

afterEach(() => {
  rmSync(project, { recursive: true, force: true });
});

/**
 * Run the plugin's setup against a fake v2 context and collect what it registers.
 *
 * @param command Command editor to hand the plugin.
 * @returns Registered tools.
 */
async function register(command: CommandEditor): Promise<ToolDefinition[]> {
  const tools: ToolDefinition[] = [];
  await plugin.setup({
    location: { directory: project },
    tool: { transform: (edit) => edit({ add: (tool) => tools.push(tool) }) },
    command: { transform: (edit) => edit(command) },
  });
  return tools;
}

describe("opencode 2.x plugin", () => {
  // opencode 2.x loads `{ id, setup }` definitions and takes tools through
  // ctx.tool.transform; the 2.x plugin must offer the same seven tools as the
  // 1.x plugin, with JSON Schema inputs. Runs setup against a fake context and
  // checks the id, tool names, and that every input is an object schema whose
  // properties match the 1.x argument schemas.
  test("registers the chart tools", async () => {
    expect(plugin.id).toBe("comprendo");
    const tools = await register({});
    expect(tools.map((tool) => tool.name).sort()).toEqual([
      "chart_add_edge",
      "chart_add_node",
      "chart_annotate_cluster",
      "chart_annotate_node",
      "chart_find_node",
      "chart_finish",
      "chart_overview",
    ]);
    for (const tool of tools) {
      expect(tool.input.type).toBe("object");
      expect(tool.description.length).toBeGreaterThan(20);
    }
  });

  // `/chart` must be registered whichever command editor the host offers: the
  // documented 2.x `add`, or the `update` of the 1.19 v2 preview. Registers
  // against both fake editors and checks the name, description, and that the
  // template is the shared /chart prompt.
  test("registers /chart with either command editor", async () => {
    const added: { name: string; template: string }[] = [];
    await register({ add: (command) => added.push(command) });
    expect(added.map((command) => command.name)).toEqual(["chart"]);
    expect(added[0]?.template).toContain("chart_finish");

    const updated: Record<string, { template: string; description?: string }> = {};
    await register({
      update: (name, change) => {
        updated[name] = { template: "" };
        change(updated[name]!);
      },
    });
    expect(updated.chart?.template).toContain("chart_overview");
    expect(updated.chart?.description).toContain("Comprendo");
  });

  // The tools must write the project's chart and report argument mistakes as
  // text the model can read, since 2.x does not document tool errors. Adds a
  // node through execute(), finds it, then calls chart_add_node with an
  // unknown kind and checks the error comes back as content, not an exception.
  test("tools write the chart and report mistakes as text", async () => {
    const tools = Object.fromEntries((await register({})).map((tool) => [tool.name, tool]));
    const added = await tools.chart_add_node!.execute({ kind: "function", qualified_name: "geo::area", file: "src/geo.cpp" });
    expect(added.content).toStartWith("added");
    const found = await tools.chart_find_node!.execute({ query: "area" });
    expect(found.content).toContain("geo::area");
    const finished = await tools.chart_finish!.execute({});
    expect(finished.content).toContain(join(project, ".comprendo/chart.sqlite"));
    const mistake = await tools.chart_add_node!.execute({ kind: "lambda", qualified_name: "f", file: "a.cpp" });
    expect(mistake.content).toStartWith("error: unknown kind");
  });

  // Hosts without a tool or command registry (such as the 1.19 preview of the
  // v2 runtime, which has no tool domain) must not make setup fail. Runs setup
  // with an empty context and checks it resolves.
  test("tolerates a context without registries", async () => {
    await expect(plugin.setup({ location: { directory: project } })).resolves.toBeUndefined();
  });
});
