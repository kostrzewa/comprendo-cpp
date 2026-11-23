/**
 * @file Tests for ids and kind names shared with the C++ tools.
 */

import { describe, expect, test } from "bun:test";

import {
  clusterId,
  edgeKindValue,
  fnv1a64Text,
  formatId,
  fromSql,
  nodeId,
  nodeKindValue,
  parseId,
  toSql,
} from "../src/ids.ts";

describe("ids", () => {
  // The plugin and the C++ extractor must compute the same id for the same
  // symbol, or annotations land on duplicates. Checks the FNV-1a vectors and
  // the node id vectors from plan/chart-schema.md#ids, which the C++ suite
  // checks too, including overloads that differ only in their signature and a
  // UTF-8 name.
  test("match the chart-schema test vectors", () => {
    expect(fnv1a64Text("")).toBe(0xcbf29ce484222325n);
    expect(fnv1a64Text("a")).toBe(0xaf63dc4c8601ec8cn);
    expect(fnv1a64Text("foobar")).toBe(0x85944171f73967e8n);
    expect(nodeId(8, "main", "src/main.cpp", "int main(int, char **)")).toBe(0x45010832c7701fd0n);
    expect(nodeId(9, "Comprendo::Chart::size", "include/Comprendo/Chart.hpp", "std::size_t () const")).toBe(
      0x2628ff0989741339n,
    );
    expect(nodeId(1, "src/main.cpp", "src/main.cpp", "")).toBe(0xaa52bbd69c1499c1n);
    expect(nodeId(8, "f", "a.cpp", "void (int)")).toBe(0x7de5b0f108cc13c0n);
    expect(nodeId(8, "f", "a.cpp", "void (double)")).toBe(0x2f0a08d143e85434n);
    expect(nodeId(8, "größe", "a.cpp", "")).not.toBe(nodeId(8, "grosse", "a.cpp", ""));
  });

  // SQLite stores ids as signed integers, so ids with the high bit set must
  // survive the trip through toSql/fromSql unchanged. Converts boundary values
  // both ways and checks the signed form of a high-bit id is negative, like
  // the C++ store writes it.
  test("round-trip through signed SQLite integers", () => {
    for (const id of [0n, 1n, 0x7fffffffffffffffn, 0x8000000000000000n, 0xffffffffffffffffn]) {
      expect(fromSql(toSql(id))).toBe(id);
    }
    expect(toSql(0xcbf29ce484222325n) < 0n).toBe(true);
    expect(fromSql(-1)).toBe(0xffffffffffffffffn);
  });

  // The model copies ids from tool output back into tool calls, so formatting
  // and parsing must agree and reject anything else. Formats an id, parses it
  // back, and checks decimal input, whitespace, overflow, and garbage.
  test("format and parse for tool arguments", () => {
    expect(formatId(0x2an)).toBe("0x000000000000002a");
    expect(parseId(formatId(0xcbf29ce484222325n))).toBe(0xcbf29ce484222325n);
    expect(parseId(" 42 ")).toBe(42n);
    expect(parseId("18446744073709551616")).toBeUndefined();
    expect(parseId("0x")).toBeUndefined();
    expect(parseId("main")).toBeUndefined();
    expect(parseId("-1")).toBeUndefined();
  });

  // Kinds travel as names in tool arguments and as integers in SQLite, so the
  // tables must follow the C++ enum order. Checks both ends of each table, case
  // insensitivity, unknown names, and that cluster ids are never the reserved 0.
  test("map kind names to enum values", () => {
    expect(nodeKindValue("other")).toBe(0);
    expect(nodeKindValue("Function")).toBe(8);
    expect(nodeKindValue("type_alias")).toBe(13);
    expect(nodeKindValue("lambda")).toBeUndefined();
    expect(edgeKindValue("calls")).toBe(3);
    expect(edgeKindValue("data_flow")).toBe(6);
    expect(edgeKindValue("owns")).toBeUndefined();
    expect(clusterId("Layout engine")).not.toBe(0n);
    expect(clusterId("Layout engine")).toBe(clusterId("Layout engine"));
  });
});
