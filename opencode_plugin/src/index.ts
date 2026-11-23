/**
 * @file opencode 1.x plugin entry point: registers `/chart` and the Comprendo MCP server.
 *
 * The chart tools come from `comprendo-cli mcp`; this plugin only adds the
 * command and the server to opencode's configuration, without overriding
 * entries the user defined.
 */

import { CHART_COMMAND, type CommandDefinition } from "./prompt.ts";
import { MCP_SERVER_NAME, mcpServer, type LocalMcpServer } from "./server.ts";

/** The parts of opencode's plugin input this plugin uses. */
export interface PluginInput {
  /** Project directory of the opencode instance. */
  directory: string;
}

/** The parts of opencode's configuration this plugin edits. */
export interface PluginConfig {
  /** Slash commands by name. */
  command?: Record<string, CommandDefinition>;
  /** MCP servers by name. */
  mcp?: Record<string, LocalMcpServer | Record<string, unknown>>;
}

/** Hooks returned to opencode. */
export interface PluginHooks {
  /**
   * Add `/chart` and the `comprendo` MCP server unless the user defined their own.
   *
   * @param config opencode configuration, modified in place.
   */
  config(config: PluginConfig): Promise<void>;
}

/**
 * Create the plugin's hooks.
 *
 * @param input Plugin input from opencode.
 * @returns Hooks.
 */
export async function ComprendoPlugin(input: PluginInput): Promise<PluginHooks> {
  return {
    async config(config) {
      config.command ??= {};
      config.command.chart ??= CHART_COMMAND;
      config.mcp ??= {};
      config.mcp[MCP_SERVER_NAME] ??= mcpServer(input.directory);
    },
  };
}

/** Plugin module: opencode requires an id for plugins loaded by path. */
export default { id: "comprendo", server: ComprendoPlugin };
