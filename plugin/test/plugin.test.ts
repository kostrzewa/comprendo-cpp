/**
 * @file Tests for the opencode 1.x plugin: `/chart` and the MCP server registration.
 */

import { describe, expect, test } from "bun:test";
import { readFileSync } from "node:fs";
import { join } from "node:path";

import { ComprendoPlugin, type PluginConfig } from "../src/index.ts";
import { CHART_COMMAND } from "../src/prompt.ts";
import { comprendoCli, mcpServer } from "../src/server.ts";

describe("ComprendoPlugin", () => {
  // opencode wires the plugin through its config hook: /chart and the
  // comprendo MCP server must appear without overriding what the user
  // defined. Applies the hook to an empty and to a customized config and
  // checks the added entries, the server's command line, and that the
  // user's own entries are kept.
  test("adds /chart and the MCP server unless the user defined them", async () => {
    const hooks = await ComprendoPlugin({ directory: "/projects/geo" });
    const config: PluginConfig = {};
    await hooks.config(config);
    expect(config.command?.chart?.template).toContain("chart_finish");
    expect(config.mcp?.comprendo).toEqual({
      type: "local",
      command: [comprendoCli(), "mcp", "--root", "/projects/geo"],
    });

    const custom: PluginConfig = {
      command: { chart: { template: "mine", description: "mine" } },
      mcp: { comprendo: { type: "remote", url: "http://localhost:1234" } },
    };
    await hooks.config(custom);
    expect(custom.command?.chart?.template).toBe("mine");
    expect(custom.mcp?.comprendo).toEqual({ type: "remote", url: "http://localhost:1234" });
  });
});

describe("prompt and server", () => {
  // The prompt is shared with the Claude Code skill through
  // prompts/chart.md, and users without comprendo-cli on PATH point
  // COMPRENDO_CLI at it. Checks the command's template is the shared file and
  // the executable falls back to PATH when the variable is unset or blank.
  test("use the shared prompt and honour COMPRENDO_CLI", () => {
    const shared = readFileSync(join(import.meta.dir, "../../prompts/chart.md"), "utf8");
    expect(CHART_COMMAND.template).toBe(shared);
    expect(CHART_COMMAND.template).toContain("$ARGUMENTS");
    expect(comprendoCli({})).toBe("comprendo-cli");
    expect(comprendoCli({ COMPRENDO_CLI: "  " })).toBe("comprendo-cli");
    expect(mcpServer("/p", { COMPRENDO_CLI: "/opt/bin/comprendo-cli" }).command).toEqual([
      "/opt/bin/comprendo-cli",
      "mcp",
      "--root",
      "/p",
    ]);
  });
});
