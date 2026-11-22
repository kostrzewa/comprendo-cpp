# Chart schema

Cross-references: [requirements](comprendo-cpp.md) · [architecture](architecture.md) · [milestones](milestones.md)

The chart is a single SQLite database, by default `.comprendo/chart.db`. Two
independent writers fill it: the opencode plugin (through `bun:sqlite`) and
`comprendo-cli extract`. The viewer opens it read-only. The DDL in
[`schema/chart.sql`](../schema/chart.sql) is the single source of truth for both
languages; the C++ build embeds it into the generated `ChartSchema.hpp`.

## File identity

| Pragma | Value | Meaning |
| --- | --- | --- |
| `application_id` | `0x434D5052` (`'CMPR'`) | Identifies a Comprendo chart |
| `user_version` | `1` | Schema version, bumped on incompatible changes |

A writer opening an existing file checks both pragmas before writing and
refuses on a mismatch. A reader refuses a newer `user_version` and reports
the supported range.

## Connection settings

Every connection runs `PRAGMA foreign_keys = ON` and `PRAGMA journal_mode = WAL`.
WAL lets the viewer read while the plugin writes.

## Tables

### `meta`

Key/value text pairs: `project`, `git_commit` (`git describe --always --dirty`),
`generated_at` (ISO-8601 UTC), `generator` (e.g. `comprendo-cli 0.1.0`),
`source_root` (absolute path).

### `nodes`

| Column | Type | Meaning |
| --- | --- | --- |
| `id` | INTEGER PK | Node id, see [Ids](#ids) |
| `kind` | INTEGER | `NodeKind`, checked `0..13` |
| `name` | TEXT | Unqualified name |
| `qualified_name` | TEXT | Fully qualified name |
| `file` | TEXT | Repository-relative path |
| `line_start` | INTEGER | First line, 1-based |
| `line_end` | INTEGER | Last line, 1-based |
| `signature` | TEXT | Declaration signature, empty when unknown |
| `summary` | TEXT | One-line model explanation |
| `detail` | TEXT | Long-form model explanation |
| `cluster_id` | INTEGER NULL | Owning cluster (FK), `NULL` when unclustered |
| `loc` | INTEGER | Lines of code |
| `complexity` | INTEGER | Cyclomatic complexity, `0` when unknown |
| `provenance` | INTEGER | `Provenance`, checked `0..2` |

#### NodeKind

```
0 Other      1 File       2 Module     3 Namespace
4 Class      5 Struct     6 Union      7 Enum
8 Function   9 Method    10 Field     11 Variable
12 Macro     13 TypeAlias
```

### `edges`

Primary key `(src, dst, kind)`; writers upsert on conflict.

| Column | Type | Meaning |
| --- | --- | --- |
| `src` | INTEGER | Source node id (FK, cascade delete) |
| `dst` | INTEGER | Destination node id (FK, cascade delete) |
| `kind` | INTEGER | `EdgeKind`, checked `0..6` |
| `weight` | REAL | Aggregated strength, default `1.0` |
| `evidence` | TEXT | File:line or short justification |
| `provenance` | INTEGER | `Provenance`, checked `0..2` |

#### EdgeKind

```
0 Other   1 Contains   2 Includes   3 Calls
4 Inherits   5 Uses    6 DataFlow
```

### `clusters`

| Column | Type | Meaning |
| --- | --- | --- |
| `id` | INTEGER PK | Cluster id, checked non-zero |
| `label` | TEXT | Display label |
| `summary` | TEXT | Model explanation |
| `color` | INTEGER | Packed `0xRRGGBB` |
| `parent_id` | INTEGER NULL | Parent cluster (FK), `NULL` for top level |

### `layout`

| Column | Type | Meaning |
| --- | --- | --- |
| `node_id` | INTEGER PK | Node id (FK, cascade delete) |
| `x` | REAL | Node x position |
| `y` | REAL | Node y position |

Positions are computed deterministically from the graph, so they are a cache
rather than an input. A layout missing any node's position is stale: the C++
reader returns no layout, and the viewer recomputes it.

## Row order

Tables carry no meaningful order. `read_chart` returns charts in canonical
order (`canonicalize()`): nodes and clusters by unsigned id, edges by
`(src, dst, kind)`. Layout positions follow their nodes.

### Provenance

```
0 Unknown   1 Extractor   2 Model
```

## Ids

Node ids are 64-bit FNV-1a (offset `0xcbf29ce484222325`, prime
`0x100000001b3`) over the bytes

```
kind_byte 0x1F qualified_name 0x1F file 0x1F signature
```

where `kind_byte` is the `NodeKind` value as one byte and strings are UTF-8.
The unsigned 64-bit result is stored bit-cast to SQLite's signed `INTEGER`, so
ids with the high bit set appear negative in the `sqlite3` shell. The plugin
opens the database with `safeIntegers: true` and handles ids as `BigInt`.

In memory, `cluster_id == 0` and `parent_id == 0` mean "none" and map to SQL
`NULL`.

Test vectors (checked by the C++ and the plugin test suites):

| Input | Hash |
| --- | --- |
| `fnv1a64("")` | `0xcbf29ce484222325` |
| `fnv1a64("a")` | `0xaf63dc4c8601ec8c` |
| `fnv1a64("foobar")` | `0x85944171f73967e8` |
| `(Function, "main", "src/main.cpp", "int main(int, char **)")` | `0x45010832c7701fd0` |
| `(Method, "Comprendo::Chart::size", "include/Comprendo/Chart.hpp", "std::size_t () const")` | `0x2628ff0989741339` |
| `(File, "src/main.cpp", "src/main.cpp", "")` | `0xaa52bbd69c1499c1` |
| `(Function, "f", "a.cpp", "void (int)")` | `0x7de5b0f108cc13c0` |
| `(Function, "f", "a.cpp", "void (double)")` | `0x2f0a08d143e85434` |

## Validation

The schema enforces most invariants at write time: unique node, cluster, and
edge keys; non-zero cluster ids; kind and provenance ranges; and every
reference (edge endpoints, `cluster_id`, `parent_id`, `layout.node_id`)
through foreign keys.

`comprendo-cli validate` additionally rejects a chart when:

- `application_id` differs or `user_version` is missing or unsupported;
- `PRAGMA integrity_check` or `PRAGMA foreign_key_check` reports problems;
- the in-memory `validate()` finds errors (the same rules, re-checked after
  load, so charts written with foreign keys disabled are still caught).

## Versioning

`user_version` starts at `1`. Additive columns with defaults and new tables do
not bump it; renames, removals, or type changes do. The viewer refuses to open
a newer version and reports the supported range.
