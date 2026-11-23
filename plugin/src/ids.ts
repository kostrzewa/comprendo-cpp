/**
 * @file Node and cluster ids, kind names, and id conversions shared with the C++ tools.
 *
 * Mirrors `make_node_id` in `include/Comprendo/Chart.hpp`; the recipe and test
 * vectors are documented in `plan/chart-schema.md#ids`.
 */

/** FNV-1a 64-bit offset basis. */
const FNV_OFFSET = 0xcbf29ce484222325n;

/** FNV-1a 64-bit prime. */
const FNV_PRIME = 0x100000001b3n;

/** Separator between the fields hashed into an id. */
const FIELD_SEPARATOR = 0x1f;

/** Node kinds by `NodeKind` value. */
export const NODE_KINDS = [
  "other",
  "file",
  "module",
  "namespace",
  "class",
  "struct",
  "union",
  "enum",
  "function",
  "method",
  "field",
  "variable",
  "macro",
  "type_alias",
] as const;

/** Edge kinds by `EdgeKind` value. */
export const EDGE_KINDS = [
  "other",
  "contains",
  "includes",
  "calls",
  "inherits",
  "uses",
  "data_flow",
] as const;

/** A node kind name. */
export type NodeKindName = (typeof NODE_KINDS)[number];

/** An edge kind name. */
export type EdgeKindName = (typeof EDGE_KINDS)[number];

/** `Provenance` values. */
export const Provenance = { Unknown: 0, Extractor: 1, Model: 2 } as const;

/** Shared UTF-8 encoder. */
const encoder = new TextEncoder();

/**
 * Hash bytes with 64-bit FNV-1a.
 *
 * @param bytes Input bytes.
 * @returns Unsigned 64-bit hash.
 */
export function fnv1a64(bytes: Uint8Array): bigint {
  let hash = FNV_OFFSET;
  for (const byte of bytes) {
    hash = BigInt.asUintN(64, (hash ^ BigInt(byte)) * FNV_PRIME);
  }
  return hash;
}

/**
 * Hash a string's UTF-8 bytes with 64-bit FNV-1a.
 *
 * @param text Input text.
 * @returns Unsigned 64-bit hash.
 */
export function fnv1a64Text(text: string): bigint {
  return fnv1a64(encoder.encode(text));
}

/**
 * Compute a node id exactly like the C++ `make_node_id`.
 *
 * @param kind Node kind value (0–13).
 * @param qualifiedName Fully qualified name.
 * @param file Repository-relative path.
 * @param signature Declaration signature.
 * @returns Unsigned 64-bit id.
 */
export function nodeId(kind: number, qualifiedName: string, file: string, signature: string): bigint {
  const fields = [qualifiedName, file, signature].map((field) => encoder.encode(field));
  const bytes = new Uint8Array(1 + fields.reduce((total, field) => total + 1 + field.length, 0));
  bytes[0] = kind;
  let offset = 1;
  for (const field of fields) {
    bytes[offset++] = FIELD_SEPARATOR;
    bytes.set(field, offset);
    offset += field.length;
  }
  return fnv1a64(bytes);
}

/**
 * Compute a cluster id from its label: FNV-1a over `"cluster" 0x1F label`,
 * with 0 (reserved for "no cluster") replaced by 1.
 *
 * @param label Cluster label.
 * @returns Unsigned 64-bit id, never 0.
 */
export function clusterId(label: string): bigint {
  const id = fnv1a64Text(`cluster\x1f${label}`);
  return id === 0n ? 1n : id;
}

/**
 * Convert an unsigned id to the signed value SQLite stores.
 *
 * @param id Unsigned 64-bit id.
 * @returns The same bits as a signed 64-bit integer.
 */
export function toSql(id: bigint): bigint {
  return BigInt.asIntN(64, id);
}

/**
 * Convert a stored signed id back to its unsigned value.
 *
 * @param value Signed 64-bit integer from SQLite.
 * @returns Unsigned 64-bit id.
 */
export function fromSql(value: bigint | number): bigint {
  return BigInt.asUintN(64, BigInt(value));
}

/**
 * Format an id the way tools show it to the model: `0x` and 16 hex digits.
 *
 * @param id Unsigned 64-bit id.
 * @returns Hex text.
 */
export function formatId(id: bigint): string {
  return `0x${id.toString(16).padStart(16, "0")}`;
}

/**
 * Parse an id given by the model: hex with `0x`, or unsigned decimal.
 *
 * @param text Id text.
 * @returns Unsigned 64-bit id, or undefined when malformed or out of range.
 */
export function parseId(text: string): bigint | undefined {
  const trimmed = text.trim();
  if (!/^(0[xX][0-9a-fA-F]{1,16}|[0-9]{1,20})$/.test(trimmed)) {
    return undefined;
  }
  const id = BigInt(trimmed);
  return id <= 0xffffffffffffffffn ? id : undefined;
}

/**
 * Look up a node kind value by name.
 *
 * @param name Kind name, e.g. "function".
 * @returns Kind value, or undefined for unknown names.
 */
export function nodeKindValue(name: string): number | undefined {
  const index = NODE_KINDS.indexOf(name.trim().toLowerCase() as NodeKindName);
  return index < 0 ? undefined : index;
}

/**
 * Look up an edge kind value by name.
 *
 * @param name Kind name, e.g. "calls".
 * @returns Kind value, or undefined for unknown names.
 */
export function edgeKindValue(name: string): number | undefined {
  const index = EDGE_KINDS.indexOf(name.trim().toLowerCase() as EdgeKindName);
  return index < 0 ? undefined : index;
}
