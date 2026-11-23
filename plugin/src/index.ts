/**
 * @file opencode plugin entry point: registers `/chart` and the chart tools.
 *
 * Has no runtime dependencies besides Bun: tool arguments are plain JSON
 * Schema (opencode accepts these alongside zod schemas), so the plugin can be
 * loaded straight from this repository by path.
 */

import { CHART_COMMAND, type CommandDefinition } from "./prompt.ts";
import { createTools, runTool, type JsonSchema, type ToolArgs } from "./tools.ts";

/** The parts of opencode's plugin input this plugin uses. */
export interface PluginInput {
  /** Project directory of the opencode instance. */
  directory: string;
}

/** The parts of opencode's tool context this plugin uses. */
export interface ToolContext {
  /** Project directory of the session. */
  directory?: string;
}

/** A tool as opencode expects it from a plugin. */
export interface PluginTool {
  /** Description shown to the model. */
  description: string;
  /** Argument schemas by name. */
  args: Record<string, JsonSchema>;
  /**
   * Run the tool.
   *
   * @param args Arguments from the model.
   * @param context Session context.
   * @returns Text shown to the model.
   */
  execute(args: ToolArgs, context: ToolContext): Promise<string>;
}

/** The opencode hooks this plugin implements. */
export interface PluginHooks {
  /**
   * Add `/chart` to the configuration unless the user defined their own.
   *
   * @param config opencode configuration, modified in place.
   */
  config(config: { command?: Record<string, CommandDefinition> }): Promise<void>;
  /** Chart tools by name. */
  tool: Record<string, PluginTool>;
}

/**
 * Create the plugin's hooks for an opencode instance.
 *
 * @param input Plugin input from opencode.
 * @returns Hooks registering `/chart` and the chart tools.
 */
export async function ComprendoPlugin(input: PluginInput): Promise<PluginHooks> {
  const tools = createTools(input.directory);
  return {
    async config(config) {
      config.command ??= {};
      config.command.chart ??= CHART_COMMAND;
    },
    tool: Object.fromEntries(
      Object.entries(tools).map(([name, spec]) => [
        name,
        {
          description: spec.description,
          args: spec.args,
          async execute(args, context) {
            const directory = context.directory ?? input.directory;
            return runTool(createTools(directory), name, args, directory);
          },
        } satisfies PluginTool,
      ]),
    ),
  };
}

/** Plugin module: opencode requires an id for plugins loaded by path. */
export default { id: "comprendo", server: ComprendoPlugin };
