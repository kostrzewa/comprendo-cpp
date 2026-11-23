/**
 * @file Tests for the opencode 2.x plugin entry: MCP server and `/chart` registration.
 */

import { describe, expect, test } from "bun:test";

import { comprendoCli } from "../../opencode_plugin/src/server.ts";
import plugin, { type CommandDefinition, type CommandEditor } from "../index.ts";

/**
 * Run the plugin's setup against a fake v2 context.
 *
 * @param command Command editor to hand the plugin.
 * @param servers MCP servers the user already configured; the plugin's are added.
 * @param prompts Collects prompts sent to sessions.
 */
async function register(
  command: CommandEditor,
  servers: Map<string, unknown> = new Map(),
  prompts: { sessionID: string; text: string; delivery?: unknown }[] = [],
): Promise<void> {
  await plugin.setup({
    location: { directory: "/projects/geo" },
    mcp: {
      transform: (edit) => edit({ get: (name) => servers.get(name), set: (name, config) => servers.set(name, config) }),
    },
    command: { transform: (edit) => edit(command) },
    session: {
      prompt: async (input) => {
        prompts.push(input);
      },
    },
  });
}

describe("opencode 2.x plugin", () => {
  // opencode 2.x loads `{ id, setup }` definitions; the chart tools come from
  // comprendo-cli's MCP server, which the plugin registers through
  // ctx.mcp.transform unless the user configured a server of that name. Runs
  // setup with an empty and a pre-filled registry and checks the entries.
  test("registers the comprendo MCP server", async () => {
    expect(plugin.id).toBe("comprendo");
    const servers = new Map<string, unknown>();
    await register({}, servers);
    expect(servers.get("comprendo")).toEqual({
      type: "local",
      command: [comprendoCli(), "mcp", "--root", "/projects/geo"],
    });
    const mine = new Map<string, unknown>([["comprendo", { type: "remote", url: "http://localhost:1234" }]]);
    await register({}, mine);
    expect(mine.get("comprendo")).toEqual({ type: "remote", url: "http://localhost:1234" });
  });

  // opencode 2.x plugin commands are code (invoking a template-only command
  // fails with "execute is not a function"), while the 1.19 preview takes a
  // template. Registers against both fake editors, invokes the 2.x command with
  // arguments, and checks it prompts the session with the filled-in template
  // and the delivery passed through; the preview gets the template itself.
  test("registers /chart with either command editor", async () => {
    const added: CommandDefinition[] = [];
    const prompts: { sessionID: string; text: string; delivery?: unknown }[] = [];
    await register({ add: (command) => added.push(command) }, new Map(), prompts);
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

  // Hosts without an MCP or command registry (such as the 1.19 preview of the
  // v2 runtime) must not make setup fail. Runs setup with an empty context
  // and checks it resolves.
  test("tolerates a context without registries", async () => {
    await expect(plugin.setup({ location: { directory: "/projects/geo" } })).resolves.toBeUndefined();
  });
});
