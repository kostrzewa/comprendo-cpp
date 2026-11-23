/**
 * @file Chart tools offered to the model: argument schemas, validation, and handlers.
 *
 * Handlers take plain argument objects, so they are tested without opencode.
 * opencode passes JSON Schema arguments through unvalidated, so every handler
 * checks its own arguments and reports mistakes as {@link ChartError}s.
 */

import { basename, isAbsolute, join, normalize, relative } from "node:path";

import { Chart, ChartError, DEFAULT_CHART_PATH, type NodeRow } from "./chart.ts";
import {
  clusterId,
  EDGE_KINDS,
  edgeKindValue,
  formatId,
  NODE_KINDS,
  nodeId,
  nodeKindValue,
  parseId,
  Provenance,
} from "./ids.ts";

/** Version written into the chart's `meta.annotator`. */
export const PLUGIN_VERSION = "0.1.0";

/** Most nodes one `chart_find_node` call returns. */
const MAX_FIND_RESULTS = 200;

/** Most files `chart_overview` lists. */
const MAX_OVERVIEW_FILES = 400;

/** A JSON Schema fragment describing one argument. */
export type JsonSchema = Record<string, unknown>;

/** Arguments as received from the model. */
export type ToolArgs = Record<string, unknown>;

/** A tool: description, argument schemas, and handler. */
export interface ToolSpec {
  /** What the tool does, shown to the model. */
  description: string;
  /** Argument schemas by name; opencode marks all of them required. */
  args: Record<string, JsonSchema>;
  /**
   * Run the tool on an open chart inside one transaction.
   *
   * @param chart Open chart.
   * @param args Arguments from the model.
   * @returns Text shown to the model.
   */
  run(chart: Chart, args: ToolArgs): string;
}

/**
 * Describe a string argument.
 *
 * @param description Meaning of the argument.
 * @returns JSON Schema.
 */
function stringArg(description: string): JsonSchema {
  return { type: "string", description };
}

/**
 * Describe an integer argument.
 *
 * @param description Meaning of the argument.
 * @returns JSON Schema.
 */
function integerArg(description: string): JsonSchema {
  return { type: "integer", description };
}

/**
 * Read a string argument; missing or null counts as empty.
 *
 * @param args Arguments.
 * @param key Argument name.
 * @param required True when an empty value is an error.
 * @returns The trimmed string.
 */
function text(args: ToolArgs, key: string, required = false): string {
  const value = args[key];
  if (value !== undefined && value !== null && typeof value !== "string") {
    throw new ChartError(`${key} must be a string`);
  }
  const result = (value ?? "").trim();
  if (required && result === "") {
    throw new ChartError(`${key} is required`);
  }
  return result;
}

/**
 * Read a non-negative integer argument; missing, null, or empty counts as the fallback.
 *
 * @param args Arguments.
 * @param key Argument name.
 * @param fallback Value when absent.
 * @returns The integer.
 */
function integer(args: ToolArgs, key: string, fallback: number): number {
  const value = args[key];
  if (value === undefined || value === null || value === "") {
    return fallback;
  }
  const parsed = typeof value === "string" ? Number(value) : value;
  if (typeof parsed !== "number" || !Number.isInteger(parsed) || parsed < 0) {
    throw new ChartError(`${key} must be a non-negative integer`);
  }
  return parsed;
}

/**
 * Read a list of strings; a single string is split at commas and whitespace.
 *
 * @param args Arguments.
 * @param key Argument name.
 * @returns Non-empty entries.
 */
function list(args: ToolArgs, key: string): string[] {
  const value = args[key];
  if (value === undefined || value === null) {
    return [];
  }
  const items = typeof value === "string" ? value.split(/[\s,]+/) : value;
  if (!Array.isArray(items) || !items.every((item) => typeof item === "string")) {
    throw new ChartError(`${key} must be a list of strings`);
  }
  return items.map((item) => item.trim()).filter((item) => item !== "");
}

/**
 * Read a node id argument and check the node exists.
 *
 * @param chart Open chart.
 * @param args Arguments.
 * @param key Argument name.
 * @returns The node.
 */
function existingNode(chart: Chart, args: ToolArgs, key: string): NodeRow {
  const raw = text(args, key, true);
  const id = parseId(raw);
  if (id === undefined) {
    throw new ChartError(`${key} '${raw}' is not a node id (use the 0x… ids chart_find_node prints)`);
  }
  const node = chart.node(id);
  if (node === undefined) {
    throw new ChartError(`no node with id ${formatId(id)}; look it up with chart_find_node`);
  }
  return node;
}

/**
 * Normalize a path argument to a repository-relative path with forward slashes.
 *
 * @param path Path from the model.
 * @param root Project directory, for absolute paths.
 * @returns Relative path.
 */
function repositoryPath(path: string, root: string): string {
  const relativePath = isAbsolute(path) ? relative(root, path) : normalize(path);
  const result = relativePath.replaceAll("\\", "/").replace(/^\.\//, "");
  if (result === "" || result === "." || result.startsWith("../")) {
    throw new ChartError(`file '${path}' is outside the project`);
  }
  return result;
}

/**
 * Name of a node kind value.
 *
 * @param kind `NodeKind` value.
 * @returns Kind name.
 */
function kindName(kind: number): string {
  return NODE_KINDS[kind] ?? "other";
}

/**
 * Unqualified name: the part after the last `::` or `.`.
 *
 * @param qualifiedName Qualified name.
 * @returns Name.
 */
function lastSegment(qualifiedName: string): string {
  return qualifiedName.split(/::|\./).at(-1) ?? qualifiedName;
}

/**
 * Parse a `#RRGGBB` color; empty picks a color from the label.
 *
 * @param color Color text.
 * @param label Cluster label.
 * @returns 0xRRGGBB.
 */
function parseColor(color: string, label: string): number {
  if (color === "") {
    const hue = Number(clusterId(label) % 360n);
    return hslToRgb(hue, 0.55, 0.6);
  }
  const match = /^#?([0-9a-fA-F]{6})$/.exec(color);
  if (match === null) {
    throw new ChartError(`color '${color}' must look like #4a90d9`);
  }
  return Number.parseInt(match[1]!, 16);
}

/**
 * Convert HSL to a packed RGB value.
 *
 * @param hue Hue in degrees.
 * @param saturation Saturation in [0, 1].
 * @param lightness Lightness in [0, 1].
 * @returns 0xRRGGBB.
 */
function hslToRgb(hue: number, saturation: number, lightness: number): number {
  const chroma = (1 - Math.abs(2 * lightness - 1)) * saturation;
  const channel = (n: number) => {
    const k = (n + hue / 30) % 12;
    const value = lightness - chroma / 2 * Math.max(-1, Math.min(k - 3, 9 - k, 1));
    return Math.round(value * 255);
  };
  return (channel(0) << 16) | (channel(8) << 8) | channel(4);
}

/**
 * Build the tool set.
 *
 * @param root Project directory, used to make absolute paths relative.
 * @returns Tools by name.
 */
export function createTools(root: string): Record<string, ToolSpec> {
  return {
    chart_overview: {
      description:
        "Summarize the Comprendo chart: node and edge counts by kind, clusters, and per file the " +
        "number of symbols and how many already have a summary. Start here.",
      args: {},
      run(chart) {
        const nodes = chart.countByKind("nodes");
        const edges = chart.countByKind("edges");
        const files = chart.files();
        const clusters = chart.clusters();
        const counts = (map: Map<number, number>, names: readonly string[]) =>
          [...map].map(([kind, count]) => `${names[kind] ?? kind} ${count}`).join(", ") || "none";
        const lines = [
          `chart: ${chart.path}`,
          `generator: ${chart.meta("generator") || "(none)"}`,
          `nodes: ${counts(nodes, NODE_KINDS)}`,
          `edges: ${counts(edges, EDGE_KINDS)}`,
          `clusters: ${clusters.map((c) => `${c.label} (${c.members})`).join(", ") || "none"}`,
          "",
          "files (symbols, annotated, file summary):",
          ...files
            .slice(0, MAX_OVERVIEW_FILES)
            .map((f) => `  ${f.file}  ${f.symbols} ${f.annotated}${f.summary ? `  — ${f.summary}` : ""}`),
        ];
        if (files.length > MAX_OVERVIEW_FILES) {
          lines.push(`  … ${files.length - MAX_OVERVIEW_FILES} more files; use chart_find_node with file`);
        }
        if (files.length === 0) {
          lines.push("  (empty: run `comprendo-cli extract`, or add nodes with chart_add_node)");
        }
        return lines.join("\n");
      },
    },

    chart_find_node: {
      description:
        "Find chart nodes and their ids. Matches the exact qualified name, the exact name, or a " +
        "substring of the qualified name; empty query lists everything in `file`. Use the printed " +
        "0x… ids in the other chart tools.",
      args: {
        query: stringArg("Qualified name, name, or part of a name; empty for all"),
        file: stringArg("Repository-relative file or directory to search in; empty for all files"),
        kind: stringArg(`Restrict to one kind (${NODE_KINDS.join(", ")}); empty for all`),
        limit: integerArg(`Maximum results (default 50, at most ${MAX_FIND_RESULTS})`),
      },
      run(chart, args) {
        const kindText = text(args, "kind");
        const kind = kindText === "" ? undefined : nodeKindValue(kindText);
        if (kindText !== "" && kind === undefined) {
          throw new ChartError(`unknown kind '${kindText}'; use one of ${NODE_KINDS.join(", ")}`);
        }
        const file = text(args, "file");
        const limit = Math.min(Math.max(integer(args, "limit", 50), 1), MAX_FIND_RESULTS);
        const nodes = chart.findNodes({
          text: text(args, "query"),
          file: file === "" ? "" : repositoryPath(file, root),
          kind,
          limit: limit + 1,
        });
        if (nodes.length === 0) {
          return "no matching nodes";
        }
        const lines = nodes.slice(0, limit).map((node) => Chart.describe(node, kindName(node.kind)));
        if (nodes.length > limit) {
          lines.push(`… more matches; narrow the query or raise limit`);
        }
        return lines.join("\n");
      },
    },

    chart_add_node: {
      description:
        "Add a symbol the chart is missing (it is placed under `parent`, or under its file). " +
        "Prefer annotating extracted nodes; use this for things the extractor cannot see.",
      args: {
        kind: stringArg(`One of ${NODE_KINDS.join(", ")}`),
        qualified_name: stringArg("Fully qualified name, e.g. geo::Circle::area"),
        file: stringArg("Repository-relative file declaring it"),
        line_start: integerArg("First line, 1-based (0 if unknown)"),
        line_end: integerArg("Last line, 1-based (0 if unknown)"),
        signature: stringArg("Signature distinguishing overloads, e.g. (double) const; may be empty"),
        summary: stringArg("One-line explanation"),
        detail: stringArg("Longer explanation; may be empty"),
        parent: stringArg("Id of the enclosing node; empty for the file"),
      },
      run(chart, args) {
        const kindText = text(args, "kind", true);
        const kind = nodeKindValue(kindText);
        if (kind === undefined) {
          throw new ChartError(`unknown kind '${kindText}'; use one of ${NODE_KINDS.join(", ")}`);
        }
        const file = repositoryPath(text(args, "file", true), root);
        const qualifiedName = kind === 1 ? file : text(args, "qualified_name", true);
        const signature = text(args, "signature");
        const lineStart = integer(args, "line_start", 0);
        const lineEnd = Math.max(integer(args, "line_end", lineStart), lineStart);
        const id = nodeId(kind, qualifiedName, file, signature);
        const existing = chart.node(id);
        if (existing !== undefined) {
          return `already in the chart: ${Chart.describe(existing, kindName(existing.kind))}`;
        }

        let parent: NodeRow | undefined;
        if (text(args, "parent") !== "") {
          parent = existingNode(chart, args, "parent");
        } else if (kind !== 1) {
          const fileId = nodeId(1, file, file, "");
          parent = chart.node(fileId);
          if (parent === undefined) {
            parent = {
              id: fileId,
              kind: 1,
              name: basename(file),
              qualifiedName: file,
              file,
              lineStart: 0,
              lineEnd: 0,
              signature: "",
              summary: "",
              detail: "",
              clusterId: 0n,
              provenance: Provenance.Model,
            };
            chart.insertNode(parent);
          }
        }
        const node: NodeRow = {
          id,
          kind,
          name: kind === 1 ? basename(file) : lastSegment(qualifiedName),
          qualifiedName,
          file,
          lineStart,
          lineEnd,
          signature,
          summary: text(args, "summary"),
          detail: text(args, "detail"),
          clusterId: 0n,
          provenance: Provenance.Model,
        };
        chart.insertNode(node);
        if (parent !== undefined) {
          chart.upsertEdge(parent.id, id, 1, 1, "");
        }
        return `added ${Chart.describe(node, kindName(kind))}`;
      },
    },

    chart_annotate_node: {
      description:
        "Explain a node: a one-line summary (what it is for) and, for central pieces, a longer " +
        "detail (how it works, invariants, pitfalls). Empty fields keep the current text.",
      args: {
        id: stringArg("Node id from chart_find_node"),
        summary: stringArg("One-line explanation; empty to keep"),
        detail: stringArg("Longer explanation in Markdown; empty to keep"),
      },
      run(chart, args) {
        const node = existingNode(chart, args, "id");
        const summary = text(args, "summary");
        const detail = text(args, "detail");
        if (summary === "" && detail === "") {
          throw new ChartError("give a summary, a detail, or both");
        }
        chart.annotateNode(node.id, summary, detail);
        return `annotated ${node.qualifiedName}`;
      },
    },

    chart_annotate_cluster: {
      description:
        "Create or update a cluster (a subsystem or feature spanning files) and assign nodes to " +
        "it. Clusters are identified by label; nodes belong to at most one cluster.",
      args: {
        label: stringArg("Short cluster name, e.g. Layout engine"),
        summary: stringArg("One-line explanation of the cluster"),
        color: stringArg("#RRGGBB, or empty to derive one from the label"),
        parent_label: stringArg("Label of an existing enclosing cluster; empty for none"),
        members: { type: "array", items: { type: "string" }, description: "Node ids to assign" },
      },
      run(chart, args) {
        const label = text(args, "label", true);
        const id = clusterId(label);
        const parentLabel = text(args, "parent_label");
        const parentId = parentLabel === "" ? 0n : clusterId(parentLabel);
        if (parentId !== 0n && chart.cluster(parentId) === undefined) {
          throw new ChartError(`no cluster labelled '${parentLabel}'; create it first`);
        }
        if (parentId !== 0n && chart.isClusterAncestor(id, parentId)) {
          throw new ChartError(`'${parentLabel}' cannot enclose '${label}': clusters would form a cycle`);
        }
        const existing = chart.cluster(id);
        const summary = text(args, "summary");
        const colorText = text(args, "color");
        chart.upsertCluster({
          id,
          label,
          summary: summary === "" ? (existing?.summary ?? "") : summary,
          color: colorText === "" && existing !== undefined ? existing.color : parseColor(colorText, label),
          parentId,
        });
        const members = list(args, "members").map((raw) => existingNode(chart, { id: raw }, "id"));
        for (const member of members) {
          chart.setNodeCluster(member.id, id);
        }
        return `${existing === undefined ? "created" : "updated"} cluster ${label} (${formatId(id)}); ` +
          `assigned ${members.length} nodes`;
      },
    },

    chart_add_edge: {
      description:
        "Record a relationship between two nodes. Existing edges of the same kind are updated.",
      args: {
        src: stringArg("Source node id"),
        dst: stringArg("Destination node id"),
        kind: stringArg(`One of ${EDGE_KINDS.join(", ")}`),
        weight: integerArg("Strength, e.g. number of uses (default 1)"),
        evidence: stringArg("Where it shows, e.g. src/a.cpp:42; may be empty"),
      },
      run(chart, args) {
        const src = existingNode(chart, args, "src");
        const dst = existingNode(chart, args, "dst");
        const kindText = text(args, "kind", true);
        const kind = edgeKindValue(kindText);
        if (kind === undefined) {
          throw new ChartError(`unknown edge kind '${kindText}'; use one of ${EDGE_KINDS.join(", ")}`);
        }
        if (src.id === dst.id) {
          throw new ChartError("an edge needs two different nodes");
        }
        const added = chart.upsertEdge(src.id, dst.id, kind, Math.max(integer(args, "weight", 1), 1), text(args, "evidence"));
        return `${added ? "added" : "updated"} ${src.qualifiedName} -${EDGE_KINDS[kind]}-> ${dst.qualifiedName}`;
      },
    },

    chart_finish: {
      description:
        "Finish charting: stamp the chart, check its integrity, and return the command that " +
        "opens it in the Comprendo viewer. Call once at the end.",
      args: {},
      run(chart) {
        const problems = chart.check();
        if (problems.length > 0) {
          throw new ChartError(`the chart has problems:\n${problems.join("\n")}`);
        }
        chart.setMeta("annotator", `comprendo-opencode ${PLUGIN_VERSION}`);
        chart.setMeta("annotated_at", new Date().toISOString());
        if (chart.meta("generator") === "") {
          chart.setMeta("generator", `comprendo-opencode ${PLUGIN_VERSION}`);
        }
        if (chart.meta("project") === "") {
          chart.setMeta("project", root.split("/").filter(Boolean).at(-1) ?? "");
        }
        const annotated = chart.files().reduce((total, f) => total + f.annotated, 0);
        const symbols = chart.files().reduce((total, f) => total + f.symbols, 0);
        return [
          `chart: ${chart.path}`,
          `${annotated} of ${symbols} symbols annotated, ${chart.clusters().length} clusters`,
          `open it with: comprendo ${chart.path}`,
        ].join("\n");
      },
    },
  };
}

/**
 * Run one tool against the project's chart: open, one transaction, close.
 *
 * @param tools Tool set from {@link createTools}.
 * @param name Tool name.
 * @param args Arguments from the model.
 * @param directory Project directory holding `.comprendo/`.
 * @returns Tool output.
 * @throws ChartError For invalid arguments or an unusable chart.
 */
export function runTool(
  tools: Record<string, ToolSpec>,
  name: string,
  args: ToolArgs,
  directory: string,
): string {
  const spec = tools[name];
  if (spec === undefined) {
    throw new ChartError(`unknown tool ${name}`);
  }
  const chart = Chart.open(join(directory, DEFAULT_CHART_PATH));
  try {
    return chart.transaction(() => spec.run(chart, args ?? {}));
  } finally {
    chart.close();
  }
}
