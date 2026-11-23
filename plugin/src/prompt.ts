/**
 * @file The `/chart` command registered in opencode.
 */

/** opencode command definition (the `command` entries of opencode's config). */
export interface CommandDefinition {
  /** Prompt sent to the model; `$ARGUMENTS` is replaced by the command's arguments. */
  template: string;
  /** Description shown in the command list. */
  description: string;
}

/** Prompt that drives the model through charting the project. */
const CHART_TEMPLATE = `Build a Comprendo code chart of this project: a map of its files, symbols, and
relationships with short explanations, stored in .comprendo/chart.sqlite and
shown by the Comprendo viewer. Focus or extra instructions from the user: $ARGUMENTS

1. Extract the structure. If \`comprendo-cli\` is on PATH (check with
   \`command -v comprendo-cli\`), run \`comprendo-cli extract\` in the project
   directory with the shell tool. It writes every file, symbol, include, call,
   and inheritance edge it can find (with clangd when a compile_commands.json
   exists). This replaces any earlier chart, including annotations. Skip this
   step when the user asked to continue an existing chart.
2. Call chart_overview to see what the chart holds.
3. Understand the code: read the README and entry points first, then the files
   the overview lists, most central first. Work file by file; never invent
   symbols you have not seen in the source.
4. Annotate as you go. Look nodes up with chart_find_node (by qualified name or
   with \`file\`) and call chart_annotate_node:
   - every file: one line on its role;
   - classes, structs, and important functions: one line on what they are for;
   - the central pieces: a detail paragraph on how they work, invariants, and
     pitfalls.
   Summaries say why something exists, not what its name already says.
5. Fill gaps. If the extractor is unavailable or missed something (macros
   expanding to code, generated code, other languages), add nodes with
   chart_add_node and relationships with chart_add_edge (calls, uses,
   inherits, includes, data_flow, other), with file:line evidence.
6. Group nodes into clusters with chart_annotate_cluster: one per subsystem or
   feature (for example "Layout engine", "SQLite store"), nested with
   parent_label when useful, each with a one-line summary and its member ids.
7. Call chart_finish and tell the user the viewer command it prints, together
   with a short tour of the clusters.`;

/** The `/chart` command. */
export const CHART_COMMAND: CommandDefinition = {
  template: CHART_TEMPLATE,
  description: "Explore the codebase and build a Comprendo chart (.comprendo/chart.sqlite)",
};
