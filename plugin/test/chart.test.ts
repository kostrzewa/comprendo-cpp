/**
 * @file Tests for opening chart databases and querying nodes.
 */

import { Database } from "bun:sqlite";
import { afterEach, beforeEach, describe, expect, test } from "bun:test";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { APPLICATION_ID, Chart, ChartError, type NodeRow } from "../src/chart.ts";
import { nodeId, Provenance } from "../src/ids.ts";

/** Scratch directory of the current test. */
let dir = "";

beforeEach(() => {
  dir = mkdtempSync(join(tmpdir(), "comprendo-plugin-"));
});

afterEach(() => {
  rmSync(dir, { recursive: true, force: true });
});

/**
 * Build a node row for tests.
 *
 * @param kind Node kind value.
 * @param qualifiedName Qualified name.
 * @param file Repository-relative file.
 * @param line First line.
 * @returns The row.
 */
function makeNode(kind: number, qualifiedName: string, file: string, line: number): NodeRow {
  return {
    id: nodeId(kind, qualifiedName, file, ""),
    kind,
    name: qualifiedName.split("::").at(-1)!,
    qualifiedName,
    file,
    lineStart: line,
    lineEnd: line,
    signature: "",
    summary: "",
    detail: "",
    clusterId: 0n,
    provenance: Provenance.Model,
  };
}

describe("Chart.open", () => {
  // The plugin may be the first writer, so opening a missing chart must create
  // the directory, the identity pragmas, and WAL mode the C++ tools expect.
  // Opens a chart in a fresh directory, closes it, and inspects the file with a
  // plain connection; then reopens it to check opening twice is harmless.
  test("creates a chart with the C++ identity", () => {
    const path = join(dir, ".comprendo/chart.sqlite");
    Chart.open(path).close();
    const db = new Database(path, { readonly: true });
    expect(db.query("PRAGMA application_id").values()[0]?.[0]).toBe(APPLICATION_ID);
    expect(db.query("PRAGMA user_version").values()[0]?.[0]).toBe(1);
    expect(db.query("PRAGMA journal_mode").values()[0]?.[0]).toBe("wal");
    db.close();
    Chart.open(path).close();
  });

  // Pointing the plugin at the wrong file must not scribble a schema into it.
  // Creates an unrelated database and a chart with a newer user_version, and
  // checks both are refused with a ChartError naming the reason, and that the
  // unrelated database gained no tables.
  test("refuses other databases and newer charts", () => {
    const other = join(dir, "other.sqlite");
    const db = new Database(other);
    db.exec("CREATE TABLE notes (text TEXT)");
    db.close();
    expect(() => Chart.open(other)).toThrow(ChartError);
    const check = new Database(other, { readonly: true });
    expect(check.query("SELECT count(*) FROM sqlite_master").values()[0]?.[0]).toBe(1);
    check.close();

    const newer = join(dir, "newer.sqlite");
    Chart.open(newer).close();
    const bump = new Database(newer);
    bump.exec("PRAGMA user_version = 2");
    bump.close();
    expect(() => Chart.open(newer)).toThrow(/version 2/);
  });

  // A plain text file at the chart path (a broken checkout, a typo) must be
  // reported, not crash the tool call with a raw SQLite error the model cannot
  // act on. Writes text where the chart should be and checks opening throws.
  test("refuses files that are not databases", () => {
    const path = join(dir, "chart.sqlite");
    writeFileSync(path, "this is not a database, but it is long enough to have a header....");
    expect(() => Chart.open(path)).toThrow();
  });
});

describe("Chart.findNodes", () => {
  // The model looks nodes up by whatever name it saw in the source, so exact
  // qualified names must rank first, then exact short names, then substrings;
  // file filters accept files and directories; LIKE wildcards in the query are
  // literal. Inserts a small set of nodes and checks each of these orderings.
  test("ranks exact matches first and filters by file", () => {
    const chart = Chart.open(join(dir, "chart.sqlite"));
    chart.transaction(() => {
      chart.insertNode(makeNode(8, "area_total", "src/z.cpp", 1));
      chart.insertNode(makeNode(9, "geo::Circle::area", "src/circle.cpp", 10));
      chart.insertNode(makeNode(9, "area", "src/a.cpp", 3));
      chart.insertNode(makeNode(8, "geo::area", "lib/area.cpp", 5));
      chart.insertNode(makeNode(8, "a_b", "src/x.cpp", 1));
    });
    const names = (text: string, file = "", kind?: number) =>
      chart.findNodes({ text, file, kind, limit: 10 }).map((node) => node.qualifiedName);

    expect(names("geo::area")).toEqual(["geo::area"]);
    expect(names("area")).toEqual(["area", "geo::area", "geo::Circle::area", "area_total"]);
    expect(names("area", "src")).toEqual(["area", "geo::Circle::area", "area_total"]);
    expect(names("area", "src/", 9)).toEqual(["area", "geo::Circle::area"]);
    expect(names("", "src/circle.cpp")).toEqual(["geo::Circle::area"]);
    expect(names("a_")).toEqual(["a_b", "area_total"]);
    expect(names("%")).toEqual([]);
    chart.close();
  });
});
