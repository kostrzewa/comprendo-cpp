/**
 * @file opencode 2.x plugin: registers `/chart` and the Comprendo MCP server through the v2 plugin API.
 *
 * The chart tools are served by `comprendo-cli mcp` (shared with the opencode
 * 1.x plugin and Claude Code); the `/chart` prompt comes from
 * `../plugin/src/prompt.ts`. The module default-exports a plain `{ id, setup }`
 * definition (what `Plugin.define` from `@opencode/plugin` returns), so it has
 * no dependencies besides Bun's built-ins.
 */

import { CHART_COMMAND } from "../plugin/src/prompt.ts";
import { MCP_SERVER_NAME, mcpServer, type LocalMcpServer } from "../plugin/src/server.ts";

/** Plugin id: scopes the plugin's storage and names it in diagnostics. */
export const PLUGIN_ID = "comprendo";

/** What a 2.x command receives when invoked. */
export interface CommandInput {
  /** Session the command runs in. */
  sessionID: string;
  /** Text typed after the command name. */
  prompt: { text: string };
  /** How the resulting prompt is delivered, passed through unchanged. */
  delivery?: unknown;
}

/** A 2.x command: code that runs when the user invokes it. */
export interface CommandDefinition {
  /** Command name, without the slash. */
  name: string;
  /** Shown in the command list. */
  description: string;
  /**
   * Run the command.
   *
   * @param input Session and arguments.
   */
  execute(input: CommandInput): Promise<void>;
}

/** The v2 command editor: `add` (2.x) or `update` (the 1.19 preview). */
export interface CommandEditor {
  /** Register a command (2.x); plugin commands are code, not templates. */
  add?(command: CommandDefinition): void;
  /** Create or change a command (1.19 preview of the v2 API). */
  update?(name: string, change: (command: { template: string; description?: string }) => void): void;
}

/** The parts of the v2 MCP editor this plugin uses. */
export interface McpEditor {
  /**
   * Look up a server.
   *
   * @param name Server name.
   * @returns Its configuration, or undefined.
   */
  get(name: string): unknown;
  /**
   * Add or replace a server.
   *
   * @param name Server name.
   * @param config Server configuration.
   */
  set(name: string, config: LocalMcpServer): void;
}

/** The parts of the v2 setup context this plugin uses. */
export interface SetupContext {
  /** Where this plugin instance was loaded. */
  location?: { directory?: string };
  /** MCP server registry. */
  mcp?: { transform(edit: (editor: McpEditor) => void): Promise<unknown> | unknown };
  /** Command registry. */
  command?: { transform(edit: (editor: CommandEditor) => void): Promise<unknown> | unknown };
  /** Session control, used to send the /chart prompt. */
  session?: {
    prompt(input: { sessionID: string; text: string; delivery?: unknown }): Promise<unknown>;
  };
}

/** Name of the slash command. */
const COMMAND_NAME = "chart";

/**
 * Fill the `/chart` template with the user's arguments.
 *
 * @param text Text typed after `/chart`.
 * @returns Prompt for the model.
 */
export function chartPrompt(text: string): string {
  return CHART_COMMAND.template.replaceAll("$ARGUMENTS", text.trim() === "" ? "(none)" : text.trim());
}

/**
 * Register `/chart` with whichever command editor the host provides.
 *
 * opencode 2.x plugin commands are code: `execute` sends the filled-in
 * template to the session as a prompt. The 1.19 preview takes a template.
 *
 * @param editor Command editor.
 * @param ctx Setup context, for prompting the session.
 */
function addChartCommand(editor: CommandEditor, ctx: SetupContext): void {
  if (typeof editor.add === "function") {
    editor.add({
      name: COMMAND_NAME,
      description: CHART_COMMAND.description,
      async execute(input) {
        await ctx.session?.prompt({
          sessionID: input.sessionID,
          text: chartPrompt(input.prompt?.text ?? ""),
          delivery: input.delivery,
        });
      },
    });
  } else if (typeof editor.update === "function") {
    editor.update(COMMAND_NAME, (command) => {
      command.template = CHART_COMMAND.template;
      command.description = CHART_COMMAND.description;
    });
  }
}

/**
 * Register the `comprendo` MCP server (unless the user configured one) and `/chart`.
 *
 * Transforms only describe registrations; opencode replays them when it
 * (re)builds its registries.
 *
 * @param ctx Setup context from opencode.
 */
export async function setup(ctx: SetupContext): Promise<void> {
  const directory = ctx.location?.directory ?? process.cwd();
  await ctx.mcp?.transform((editor) => {
    if (editor.get(MCP_SERVER_NAME) === undefined) {
      editor.set(MCP_SERVER_NAME, mcpServer(directory));
    }
  });
  await ctx.command?.transform((editor) => addChartCommand(editor, ctx));
}

/** Plugin definition, as `Plugin.define({ id, setup })` would return it. */
export default { id: PLUGIN_ID, setup };
