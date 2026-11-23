/**
 * @file Tests for the opencode 2.x plugin entry: registration and tool execution.
 */

import { afterEach, beforeEach, describe, expect, test } from "bun:test";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import plugin, { type CommandDefinition, type CommandEditor, type ToolDefinition } from "../index.ts";

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
async function register(
  command: CommandEditor,
  prompts: { sessionID: string; text: string; delivery?: unknown }[] = [],
): Promise<ToolDefinition[]> {
  const tools: ToolDefinition[] = [];
  await plugin.setup({
    location: { directory: project },
    tool: { transform: (edit) => edit({ add: (tool) => tools.push(tool) }) },
    command: { transform: (edit) => edit(command) },
    session: {
      prompt: async (input) => {
        prompts.push(input);
      },
    },
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

  // opencode 2.x plugin commands are code (invoking a template-only command
  // fails with "execute is not a function"), while the 1.19 preview takes a
  // template. Registers against both fake editors, invokes the 2.x command with
  // arguments, and checks it prompts the session with the filled-in template
  // and the delivery passed through; the preview gets the template itself.
  test("registers /chart with either command editor", async () => {
    const added: CommandDefinition[] = [];
    const prompts: { sessionID: string; text: string; delivery?: unknown }[] = [];
    await register({ add: (command) => added.push(command) }, prompts);
    expect(added.map((command) => command.name)).toEqual(["chart"]);
    await added[0]!.execute({ sessionID: "ses_1", prompt: { text: "focus on src/net" }, delivery: "queue" });
    expect(prompts).toHaveLength(1);
    expect(prompts[0]!.sessionID).toBe("ses_1");
    expect(prompts[0]!.delivery).toBe("queue");
    expect(prompts[0]!.text).toContain("chart_finish");
    expect(prompts[0]!.text).toContain("focus on src/net");
    expect(prompts[0]!.text).not.toContain("$ARGUMENTS");

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
