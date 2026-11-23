---
description: Explore the codebase and build a Comprendo chart (.comprendo/chart.sqlite) for the Comprendo viewer
argument-hint: "[focus or instructions]"
disable-model-invocation: true
allowed-tools: mcp__plugin_comprendo_chart__*, Bash(comprendo-cli extract:*)
---

Build a Comprendo code chart of this project: a map of its files, symbols, and
relationships with short explanations, stored in .comprendo/chart.sqlite and
shown by the Comprendo viewer. Focus or extra instructions from the user: $ARGUMENTS

The chart_* tools below may carry a prefix your host adds to tool names (for
example mcp__plugin_comprendo_chart__chart_overview).

1. Extract the structure: run `comprendo-cli extract` in the project directory
   with the shell tool (`"$COMPRENDO_CLI" extract` when that variable is set).
   It writes every file, symbol, include, call, and inheritance edge it can
   find (with clangd when a compile_commands.json exists), and copies Doxygen
   comments into summaries and details. This replaces any earlier chart,
   including annotations. Skip this step when the user asked to continue an
   existing chart; if the command is not found, carry on with the tools and
   add what you read with chart_add_node.
2. Call chart_overview to see what the chart holds.
3. Understand the code: read the README and entry points first, then the files
   the overview lists, most central first. Work file by file; never invent
   symbols you have not seen in the source.
4. Annotate as you go. Look nodes up with chart_find_node (by qualified name or
   with `file`) and call chart_annotate_node:
   - every file: one line on its role;
   - classes, structs, and important functions: one line on what they are for;
   - the central pieces: a detail paragraph on how they work, invariants, and
     pitfalls.
   Summaries say why something exists, not what its name already says.
   Keep summaries to one sentence and details to two to four sentences.
5. Use the code's own comments. Summaries marked [doc] in chart_find_node (and
   counted as "from code docs" in chart_overview) were copied from Doxygen
   comments; other comments you see while reading say what the authors
   intended. Treat both as the starting point, not as text to copy:
   - leave an accurate, specific [doc] summary alone;
   - rewrite it only to correct it or to add what it misses, usually why the
     code exists or how it fits with the rest;
   - never make an explanation longer because a long comment exists.
   Spend most of your effort on undocumented code, on files, and on clusters,
   which comments rarely explain.
6. Fill gaps. If the extractor is unavailable or missed something (macros
   expanding to code, generated code, other languages), add nodes with
   chart_add_node and relationships with chart_add_edge (calls, uses,
   inherits, includes, data_flow, other), with file:line evidence.
7. Group nodes into clusters with chart_annotate_cluster: one per subsystem or
   feature (for example "Layout engine", "SQLite store"), nested with
   parent_label when useful, each with a one-line summary and its member ids.
8. Call chart_finish and tell the user the viewer command it prints, together
   with a short tour of the clusters.
