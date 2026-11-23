/**
 * @file The Comprendo MCP server as opencode starts it: `comprendo-cli mcp`.
 *
 * The chart tools (`chart_overview`, `chart_annotate_node`, ...) are served by
 * the C++ CLI, so opencode 1.x, opencode 2.x, and Claude Code share one
 * implementation.
 */

/** Name of the MCP server in opencode's configuration. */
export const MCP_SERVER_NAME = "comprendo";

/** A local (stdio) MCP server entry of opencode's configuration. */
export interface LocalMcpServer {
  /** Always `local`: opencode starts the command and talks over stdio. */
  type: "local";
  /** Executable followed by its arguments. */
  command: string[];
}

/**
 * The `comprendo-cli` executable: `$COMPRENDO_CLI` when set, otherwise the
 * one on `PATH`.
 *
 * @param environment Environment to read.
 * @returns Executable name or path.
 */
export function comprendoCli(environment: Record<string, string | undefined> = process.env): string {
  const configured = environment.COMPRENDO_CLI?.trim();
  return configured ? configured : "comprendo-cli";
}

/**
 * Describe the MCP server for a project.
 *
 * @param directory Project whose `.comprendo/chart.sqlite` the tools edit.
 * @param environment Environment to read `COMPRENDO_CLI` from.
 * @returns Server entry for opencode's configuration.
 */
export function mcpServer(
  directory: string,
  environment: Record<string, string | undefined> = process.env,
): LocalMcpServer {
  return { type: "local", command: [comprendoCli(environment), "mcp", "--root", directory] };
}
