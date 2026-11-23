/**
 * @file opencode 2.x plugin: registers `/chart` and the chart tools through the v2 plugin API.
 *
 * The chart logic, the `/chart` prompt, and the embedded schema are shared
 * with the opencode 1.x plugin in `../plugin/src`, so both write identical
 * charts. The module default-exports a plain `{ id, setup }` definition (what
 * `Plugin.define` from `@opencode/plugin` returns), so it has no dependencies
 * besides Bun's built-ins.
 */

import { ChartError } from "../plugin/src/chart.ts";
import { CHART_COMMAND } from "../plugin/src/prompt.ts";
import { createTools, runTool, type JsonSchema, type ToolArgs } from "../plugin/src/tools.ts";

/** Plugin id: scopes the plugin's storage and names it in diagnostics. */
export const PLUGIN_ID = "comprendo";

/** A tool as the v2 tool editor takes it. */
export interface ToolDefinition {
  /** Tool name shown to the model. */
  name: string;
  /** What the tool does. */
  description: string;
  /** JSON Schema of the input object. */
  input: JsonSchema;
  /**
   * Run the tool.
   *
   * @param input Arguments from the model.
   * @returns Tool output.
   */
  execute(input: ToolArgs): Promise<{ content: string }>;
}

/** The parts of the v2 tool editor this plugin uses. */
export interface ToolEditor {
  /**
   * Register a tool.
   *
   * @param tool Tool definition.
   */
  add(tool: ToolDefinition): void;
}

/** The v2 command editor: `add` (2.x) or `update` (the 1.19 preview). */
export interface CommandEditor {
  /** Register a command (2.x). */
  add?(command: { name: string; description: string; template: string }): void;
  /** Create or change a command (1.19 preview of the v2 API). */
  update?(name: string, change: (command: { template: string; description?: string }) => void): void;
}

/** The parts of the v2 setup context this plugin uses. */
export interface SetupContext {
  /** Where this plugin instance was loaded. */
  location?: { directory?: string };
  /** Tool registry. */
  tool?: { transform(edit: (editor: ToolEditor) => void): Promise<unknown> | unknown };
  /** Command registry. */
  command?: { transform(edit: (editor: CommandEditor) => void): Promise<unknown> | unknown };
}

/** Name of the slash command. */
const COMMAND_NAME = "chart";

/**
 * Describe the chart tools for the v2 tool editor.
 *
 * Arguments use the same JSON Schemas as the 1.x plugin; none is required
 * because the handlers validate their own arguments and report mistakes as
 * text the model can act on.
 *
 * @param directory Project directory holding `.comprendo/`.
 * @returns Tool definitions in registration order.
 */
export function chartTools(directory: string): ToolDefinition[] {
  const tools = createTools(directory);
  return Object.entries(tools).map(([name, spec]) => ({
    name,
    description: spec.description,
    input: { type: "object", properties: spec.args, additionalProperties: false },
    async execute(input: ToolArgs) {
      try {
        return { content: runTool(tools, name, input ?? {}, directory) };
      } catch (error) {
        if (error instanceof ChartError) {
          return { content: `error: ${error.message}` };
        }
        throw error;
      }
    },
  }));
}

/**
 * Register `/chart` with whichever command editor the host provides.
 *
 * @param editor Command editor.
 */
function addChartCommand(editor: CommandEditor): void {
  if (typeof editor.add === "function") {
    editor.add({ name: COMMAND_NAME, ...CHART_COMMAND });
  } else if (typeof editor.update === "function") {
    editor.update(COMMAND_NAME, (command) => {
      command.template = CHART_COMMAND.template;
      command.description = CHART_COMMAND.description;
    });
  }
}

/**
 * Register the chart tools and `/chart`.
 *
 * Transforms only describe registrations (opencode replays them); the tools
 * open the chart when executed.
 *
 * @param ctx Setup context from opencode.
 */
export async function setup(ctx: SetupContext): Promise<void> {
  const directory = ctx.location?.directory ?? process.cwd();
  const tools = chartTools(directory);
  await ctx.tool?.transform((editor) => {
    for (const tool of tools) {
      editor.add(tool);
    }
  });
  await ctx.command?.transform(addChartCommand);
}

/** Plugin definition, as `Plugin.define({ id, setup })` would return it. */
export default { id: PLUGIN_ID, setup };
