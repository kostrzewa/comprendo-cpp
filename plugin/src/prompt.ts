/**
 * @file The `/chart` command shared by the opencode 1.x and 2.x plugins.
 *
 * The prompt itself lives in `prompts/chart.md` at the repository root, the
 * single source the Claude Code skill is generated from as well.
 */

import CHART_TEMPLATE from "../../prompts/chart.md" with { type: "text" };

/** opencode command definition (the `command` entries of opencode's config). */
export interface CommandDefinition {
  /** Prompt sent to the model; `$ARGUMENTS` is replaced by the command's arguments. */
  template: string;
  /** Description shown in the command list. */
  description: string;
}

/** The `/chart` command. */
export const CHART_COMMAND: CommandDefinition = {
  template: CHART_TEMPLATE,
  description: "Explore the codebase and build a Comprendo chart (.comprendo/chart.sqlite)",
};
