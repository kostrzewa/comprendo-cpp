/**
 * @file The chart database as the plugin sees it: opening, identity checks, and row access.
 *
 * Uses the same DDL as the C++ tools (`schema/chart.sql`, embedded in
 * `schema.ts`) and the conventions
 * of `plan/chart-schema.md`: ids are unsigned 64-bit values stored bit-cast to
 * signed `INTEGER`, and `NULL` means "no cluster".
 */

import { Database } from "bun:sqlite";
import { mkdirSync } from "node:fs";
import { dirname } from "node:path";

import { formatId, fromSql, Provenance, toSql } from "./ids.ts";
import { CHART_SCHEMA } from "./schema.ts";

/** `PRAGMA application_id` of a chart ('CMPR'). */
export const APPLICATION_ID = 0x434d5052;

/** Newest schema version this plugin writes. */
export const SCHEMA_VERSION = 1;

/** Chart location relative to the project directory. */
export const DEFAULT_CHART_PATH = ".comprendo/chart.sqlite";

/** How long a write waits for another writer (the CLI, the viewer's reader). */
const BUSY_TIMEOUT_MS = 5000;

/** A failure the model can act on, reported as the tool's error. */
export class ChartError extends Error {}

/** A node row with ids as unsigned values. */
export interface NodeRow {
  /** Node id. */
  id: bigint;
  /** `NodeKind` value. */
  kind: number;
  /** Unqualified name. */
  name: string;
  /** Fully qualified name. */
  qualifiedName: string;
  /** Repository-relative file. */
  file: string;
  /** First line, 1-based (0 when unknown). */
  lineStart: number;
  /** Last line, 1-based (0 when unknown). */
  lineEnd: number;
  /** Declaration signature. */
  signature: string;
  /** One-line explanation. */
  summary: string;
  /** Long-form explanation. */
  detail: string;
  /** Owning cluster, or 0n for none. */
  clusterId: bigint;
  /** `Provenance` value. */
  provenance: number;
}

/** A cluster row with ids as unsigned values. */
export interface ClusterRow {
  /** Cluster id (never 0). */
  id: bigint;
  /** Display label. */
  label: string;
  /** One-line explanation. */
  summary: string;
  /** 0xRRGGBB, or 0 to let the viewer choose. */
  color: number;
  /** Parent cluster, or 0n for none. */
  parentId: bigint;
}

/** Annotation progress of one file, from {@link Chart.files}. */
export interface FileProgress {
  /** Repository-relative file. */
  file: string;
  /** Symbols in the file, the file node excluded. */
  symbols: number;
  /** Symbols whose summary came from code comments. */
  documented: number;
  /** Symbols whose summary the model wrote. */
  annotated: number;
  /** Summary of the file node itself. */
  summary: string;
}

/** Search options for {@link Chart.findNodes}. */
export interface NodeQuery {
  /** Exact qualified name, exact name, or substring of the qualified name; empty for all. */
  text: string;
  /** Exact file or directory prefix; empty for all files. */
  file: string;
  /** `NodeKind` value to restrict to, or undefined for all kinds. */
  kind?: number;
  /** Largest number of rows returned. */
  limit: number;
}

/** Raw node row as selected from SQLite with `safeIntegers`. */
interface RawNode {
  id: bigint;
  kind: bigint;
  name: string;
  qualified_name: string;
  file: string;
  line_start: bigint;
  line_end: bigint;
  signature: string;
  summary: string;
  detail: string;
  cluster_id: bigint | null;
  provenance: bigint;
}

/** Columns selected for {@link RawNode}. */
const NODE_COLUMNS =
  "id, kind, name, qualified_name, file, line_start, line_end, signature, summary, detail, cluster_id, provenance";

/**
 * Convert a raw node row.
 *
 * @param raw Row from SQLite.
 * @returns The node with unsigned ids and numeric fields.
 */
function toNode(raw: RawNode): NodeRow {
  return {
    id: fromSql(raw.id),
    kind: Number(raw.kind),
    name: raw.name,
    qualifiedName: raw.qualified_name,
    file: raw.file,
    lineStart: Number(raw.line_start),
    lineEnd: Number(raw.line_end),
    signature: raw.signature,
    summary: raw.summary,
    detail: raw.detail,
    clusterId: raw.cluster_id === null ? 0n : fromSql(raw.cluster_id),
    provenance: Number(raw.provenance),
  };
}

/**
 * Escape `%`, `_`, and the escape character for a `LIKE ... ESCAPE '\'` pattern.
 *
 * @param text Literal text.
 * @returns Escaped text.
 */
function escapeLike(text: string): string {
  return text.replace(/[\\%_]/g, (c) => `\\${c}`);
}

/** An open chart database. */
export class Chart {
  /** Underlying connection. */
  readonly #db: Database;

  /** Chart file. */
  readonly path: string;

  /**
   * Wrap an opened connection.
   *
   * @param db Connection with pragmas and schema applied.
   * @param path Chart file.
   */
  private constructor(db: Database, path: string) {
    this.#db = db;
    this.path = path;
  }

  /**
   * Open a chart, creating it (and its directory) when absent.
   *
   * Refuses files that are not charts and charts with a newer schema, then
   * enables WAL and foreign keys and applies the idempotent schema.
   *
   * @param path Chart file.
   * @param schema Chart DDL.
   * @returns The open chart.
   * @throws ChartError When the file is not a usable chart.
   */
  static open(path: string, schema: string = CHART_SCHEMA): Chart {
    mkdirSync(dirname(path), { recursive: true });
    const db = new Database(path, { create: true, safeIntegers: true, strict: true });
    try {
      db.exec(`PRAGMA busy_timeout = ${BUSY_TIMEOUT_MS}`);
      const pragma = (name: string) =>
        Number((db.query(`PRAGMA ${name}`).values()[0] ?? [0])[0]);
      const applicationId = pragma("application_id");
      const version = pragma("user_version");
      const objects = Number(
        (db.query("SELECT count(*) FROM sqlite_master").values()[0] ?? [0])[0],
      );
      if (applicationId !== APPLICATION_ID && (applicationId !== 0 || objects > 0)) {
        throw new ChartError(`${path} is not a Comprendo chart`);
      }
      if (version > SCHEMA_VERSION) {
        throw new ChartError(
          `${path} has chart schema version ${version}; this plugin supports 1..${SCHEMA_VERSION}`,
        );
      }
      db.exec("PRAGMA journal_mode = WAL");
      db.exec("PRAGMA foreign_keys = ON");
      db.exec(schema);
      return new Chart(db, path);
    } catch (error) {
      db.close();
      throw error;
    }
  }

  /** Close the connection. */
  close(): void {
    this.#db.close();
  }

  /**
   * Run a function in one immediate transaction.
   *
   * @param work Function doing the reads and writes.
   * @returns What \p work returns; the transaction rolls back when it throws.
   */
  transaction<T>(work: () => T): T {
    return this.#db.transaction(work).immediate();
  }

  /**
   * Look up a node.
   *
   * @param id Node id.
   * @returns The node, or undefined.
   */
  node(id: bigint): NodeRow | undefined {
    const raw = this.#db
      .query<RawNode, [bigint]>(`SELECT ${NODE_COLUMNS} FROM nodes WHERE id = ?`)
      .get(toSql(id));
    return raw === null ? undefined : toNode(raw);
  }

  /**
   * Find nodes, best matches first: exact qualified name, exact name, then
   * qualified names containing the text; ties ordered by file and line.
   *
   * @param query Search options.
   * @returns Matching nodes.
   */
  findNodes(query: NodeQuery): NodeRow[] {
    const file = query.file.replace(/\/+$/, "");
    const raws = this.#db
      .query<RawNode, Record<string, string | number | bigint | null>>(
        `SELECT ${NODE_COLUMNS},
           CASE WHEN qualified_name = $text THEN 0 WHEN name = $text THEN 1 ELSE 2 END AS rank
         FROM nodes
         WHERE ($text = '' OR qualified_name = $text OR name = $text
                OR qualified_name LIKE $like ESCAPE '\\')
           AND ($file = '' OR file = $file OR file LIKE $prefix ESCAPE '\\')
           AND ($kind IS NULL OR kind = $kind)
         ORDER BY rank, file, line_start, qualified_name
         LIMIT $limit`,
      )
      .all({
        text: query.text,
        like: `%${escapeLike(query.text)}%`,
        file,
        prefix: `${escapeLike(file)}/%`,
        kind: query.kind ?? null,
        limit: query.limit,
      });
    return raws.map(toNode);
  }

  /**
   * Insert a node with provenance `Model`. Stale layout rows are removed so the
   * viewer lays out the changed chart again.
   *
   * @param node Node to insert; its id must be new.
   */
  insertNode(node: NodeRow): void {
    this.#db
      .query(
        `INSERT INTO nodes (id, kind, name, qualified_name, file, line_start, line_end,
                            signature, summary, detail, cluster_id, provenance)
         VALUES ($id, $kind, $name, $qualified_name, $file, $line_start, $line_end,
                 $signature, $summary, $detail, $cluster_id, $provenance)`,
      )
      .run({
        id: toSql(node.id),
        kind: node.kind,
        name: node.name,
        qualified_name: node.qualifiedName,
        file: node.file,
        line_start: node.lineStart,
        line_end: node.lineEnd,
        signature: node.signature,
        summary: node.summary,
        detail: node.detail,
        cluster_id: node.clusterId === 0n ? null : toSql(node.clusterId),
        provenance: node.provenance,
      });
    this.#db.exec("DELETE FROM layout");
  }

  /**
   * Set a node's explanation; empty values keep the current text. The node's
   * provenance becomes `Model`, since its annotation now comes from the model.
   *
   * @param id Node id.
   * @param summary New one-line summary, or empty.
   * @param detail New long-form detail, or empty.
   */
  annotateNode(id: bigint, summary: string, detail: string): void {
    this.#db
      .query(
        `UPDATE nodes SET
           summary = CASE WHEN $summary = '' THEN summary ELSE $summary END,
           detail = CASE WHEN $detail = '' THEN detail ELSE $detail END,
           provenance = $provenance
         WHERE id = $id`,
      )
      .run({ id: toSql(id), summary, detail, provenance: Provenance.Model });
  }

  /**
   * Insert or update an edge. An existing edge keeps its provenance and, when
   * \p evidence is empty, its evidence.
   *
   * @param src Source node id.
   * @param dst Destination node id.
   * @param kind `EdgeKind` value.
   * @param weight Edge weight.
   * @param evidence Where the relationship shows, e.g. `file:line`.
   * @returns True when the edge was new.
   */
  upsertEdge(src: bigint, dst: bigint, kind: number, weight: number, evidence: string): boolean {
    const existed =
      this.#db
        .query("SELECT 1 FROM edges WHERE src = ? AND dst = ? AND kind = ?")
        .get(toSql(src), toSql(dst), kind) !== null;
    this.#db
      .query(
        `INSERT INTO edges (src, dst, kind, weight, evidence, provenance)
         VALUES ($src, $dst, $kind, $weight, $evidence, $provenance)
         ON CONFLICT (src, dst, kind) DO UPDATE SET
           weight = excluded.weight,
           evidence = CASE WHEN excluded.evidence = '' THEN edges.evidence ELSE excluded.evidence END`,
      )
      .run({
        src: toSql(src),
        dst: toSql(dst),
        kind,
        weight,
        evidence,
        provenance: Provenance.Model,
      });
    return !existed;
  }

  /**
   * Look up a cluster.
   *
   * @param id Cluster id.
   * @returns The cluster, or undefined.
   */
  cluster(id: bigint): ClusterRow | undefined {
    const raw = this.#db
      .query<
        { id: bigint; label: string; summary: string; color: bigint; parent_id: bigint | null },
        [bigint]
      >("SELECT id, label, summary, color, parent_id FROM clusters WHERE id = ?")
      .get(toSql(id));
    return raw === null
      ? undefined
      : {
          id: fromSql(raw.id),
          label: raw.label,
          summary: raw.summary,
          color: Number(raw.color),
          parentId: raw.parent_id === null ? 0n : fromSql(raw.parent_id),
        };
  }

  /**
   * Insert or replace a cluster's fields.
   *
   * @param cluster Cluster to write.
   */
  upsertCluster(cluster: ClusterRow): void {
    this.#db
      .query(
        `INSERT INTO clusters (id, label, summary, color, parent_id)
         VALUES ($id, $label, $summary, $color, $parent_id)
         ON CONFLICT (id) DO UPDATE SET
           label = excluded.label, summary = excluded.summary,
           color = excluded.color, parent_id = excluded.parent_id`,
      )
      .run({
        id: toSql(cluster.id),
        label: cluster.label,
        summary: cluster.summary,
        color: cluster.color,
        parent_id: cluster.parentId === 0n ? null : toSql(cluster.parentId),
      });
  }

  /**
   * Check whether making \p parent the parent of \p id would create a cycle.
   *
   * @param id Cluster id.
   * @param parent Proposed parent id.
   * @returns True when \p id is \p parent or one of its ancestors.
   */
  isClusterAncestor(id: bigint, parent: bigint): boolean {
    for (let current = parent, steps = 0; current !== 0n; steps++) {
      if (current === id || steps > 10000) {
        return true;
      }
      current = this.cluster(current)?.parentId ?? 0n;
    }
    return false;
  }

  /**
   * Assign a node to a cluster.
   *
   * @param node Node id.
   * @param cluster Cluster id, or 0n to remove the node from its cluster.
   */
  setNodeCluster(node: bigint, cluster: bigint): void {
    this.#db
      .query("UPDATE nodes SET cluster_id = ? WHERE id = ?")
      .run(cluster === 0n ? null : toSql(cluster), toSql(node));
  }

  /**
   * Read a `meta` value.
   *
   * @param key Key.
   * @returns The value, or an empty string.
   */
  meta(key: string): string {
    return (
      this.#db
        .query<{ value: string }, [string]>("SELECT value FROM meta WHERE key = ?")
        .get(key)?.value ?? ""
    );
  }

  /**
   * Write a `meta` value.
   *
   * @param key Key.
   * @param value Value.
   */
  setMeta(key: string, value: string): void {
    this.#db
      .query("INSERT INTO meta (key, value) VALUES (?, ?) ON CONFLICT (key) DO UPDATE SET value = excluded.value")
      .run(key, value);
  }

  /**
   * Count rows grouped by a column.
   *
   * @param table `nodes` or `edges`.
   * @returns Count per `kind` value.
   */
  countByKind(table: "nodes" | "edges"): Map<number, number> {
    const rows = this.#db
      .query<{ kind: bigint; count: bigint }, []>(
        `SELECT kind, count(*) AS count FROM ${table} GROUP BY kind ORDER BY kind`,
      )
      .all();
    return new Map(rows.map((row) => [Number(row.kind), Number(row.count)]));
  }

  /**
   * Summarize the chart per file: symbol count, how many summaries came from
   * code comments (provenance `Extractor`), and how many the model wrote.
   *
   * @returns One entry per file, ordered by path.
   */
  files(): FileProgress[] {
    return this.#db
      .query<
        { file: string; symbols: bigint; documented: bigint; annotated: bigint; summary: string | null },
        []
      >(
        `SELECT n.file AS file,
           sum(n.kind <> 1) AS symbols,
           sum(n.kind <> 1 AND n.summary <> '' AND n.provenance <> ${Provenance.Model}) AS documented,
           sum(n.kind <> 1 AND n.summary <> '' AND n.provenance = ${Provenance.Model}) AS annotated,
           max(CASE WHEN n.kind = 1 THEN n.summary END) AS summary
         FROM nodes n GROUP BY n.file ORDER BY n.file`,
      )
      .all()
      .map((row) => ({
        file: row.file,
        symbols: Number(row.symbols),
        documented: Number(row.documented),
        annotated: Number(row.annotated),
        summary: row.summary ?? "",
      }));
  }

  /**
   * List the clusters with their member counts.
   *
   * @returns Clusters ordered by label.
   */
  clusters(): { id: bigint; label: string; parentId: bigint; members: number }[] {
    return this.#db
      .query<{ id: bigint; label: string; parent_id: bigint | null; members: bigint }, []>(
        `SELECT c.id, c.label, c.parent_id,
           (SELECT count(*) FROM nodes n WHERE n.cluster_id = c.id) AS members
         FROM clusters c ORDER BY c.label`,
      )
      .all()
      .map((row) => ({
        id: fromSql(row.id),
        label: row.label,
        parentId: row.parent_id === null ? 0n : fromSql(row.parent_id),
        members: Number(row.members),
      }));
  }

  /**
   * Run SQLite's integrity and foreign key checks.
   *
   * @returns Problems found; empty when the chart is sound.
   */
  check(): string[] {
    const problems: string[] = [];
    for (const [message] of this.#db.query<[string], []>("PRAGMA integrity_check").values()) {
      if (message !== "ok") {
        problems.push(message);
      }
    }
    for (const row of this.#db.query("PRAGMA foreign_key_check").values()) {
      problems.push(`foreign key violation in ${String(row[0])} row ${String(row[1])}`);
    }
    return problems;
  }

  /**
   * Describe a node in one line for tool output.
   *
   * @param node Node.
   * @param kindName Name of the node's kind.
   * @returns `id kind qualified_name signature file:start-end`, plus the
   *          summary, marked `[doc]` when it came from code comments.
   */
  static describe(node: NodeRow, kindName: string): string {
    const lines = node.lineStart > 0 ? `:${node.lineStart}-${node.lineEnd}` : "";
    const signature = node.signature === "" ? "" : ` ${node.signature}`;
    const source = node.provenance === Provenance.Model ? "" : "[doc] ";
    const summary = node.summary === "" ? "" : ` — ${source}${node.summary}`;
    return `${formatId(node.id)} ${kindName} ${node.qualifiedName}${signature} ${node.file}${lines}${summary}`;
  }
}
