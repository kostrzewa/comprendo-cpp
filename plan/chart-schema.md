# Chart schema

Cross-references: [requirements](comprendo-cpp.md) · [architecture](architecture.md) · [milestones](milestones.md)

The chart is a single HDF5 file, by default `.comprendo/chart.h5`. It is written
only by `comprendo-cli import` and read only by the viewer. The layout is
columnar: every node, edge, and cluster attribute is a parallel dataset, which
keeps highfive usage simple and makes partial reads cheap.

## Root attributes

| Attribute | Type | Meaning |
| --- | --- | --- |
| `schema_version` | uint32 | Bumped on incompatible layout changes |
| `project` | string | Project name |
| `git_commit` | string | `git describe --always --dirty` at generation time |
| `generated_at` | string | ISO-8601 UTC timestamp |
| `generator` | string | `comprendo-cli <version>` |
| `source_root` | string | Absolute path the chart was extracted from |

## Nodes

Group `/nodes`, one dataset per attribute, all length `N`:

| Dataset | Type | Meaning |
| --- | --- | --- |
| `id` | uint64 | FNV-1a over `(kind, qualified_name, file, signature)` |
| `kind` | uint8 | `NodeKind` enum |
| `name` | string | Unqualified name |
| `qualified_name` | string | Fully qualified name |
| `file` | string | Repository-relative path |
| `line_start` | uint32 | First line, 1-based |
| `line_end` | uint32 | Last line, 1-based |
| `signature` | string | Declaration signature, empty when unknown |
| `summary` | string | One-line model explanation |
| `detail` | string | Long-form model explanation |
| `cluster_id` | uint64 | Owning cluster, `0` when unclustered |
| `loc` | uint32 | Lines of code |
| `complexity` | uint32 | Cyclomatic complexity, `0` when unknown |
| `provenance` | uint8 | `Provenance` enum |

### NodeKind

```
0 Other      1 File       2 Module     3 Namespace
4 Class      5 Struct     6 Union      7 Enum
8 Function   9 Method    10 Field     11 Variable
12 Macro     13 TypeAlias
```

## Edges

Group `/edges`, all length `M`:

| Dataset | Type | Meaning |
| --- | --- | --- |
| `src` | uint64 | Source node id |
| `dst` | uint64 | Destination node id |
| `kind` | uint8 | `EdgeKind` enum |
| `weight` | float32 | Aggregated strength, default `1.0` |
| `evidence` | string | File:line or short justification |
| `provenance` | uint8 | `Provenance` enum |

### EdgeKind

```
0 Other   1 Contains   2 Includes   3 Calls
4 Inherits   5 Uses    6 DataFlow
```

## Clusters

Group `/clusters`, all length `K`:

| Dataset | Type | Meaning |
| --- | --- | --- |
| `id` | uint64 | Cluster id, non-zero |
| `label` | string | Display label |
| `summary` | string | Model explanation |
| `color` | uint32 | Packed `0xRRGGBB` |
| `parent_id` | uint64 | Parent cluster, `0` for top level |

## Layout

Group `/layout`:

| Dataset | Type | Meaning |
| --- | --- | --- |
| `x` | float32 | Node x position, length `N` |
| `y` | float32 | Node y position, length `N` |

Positions are computed deterministically from the graph, so they are a cache
rather than an input. The viewer may recompute them when the dataset is absent.

## Provenance

```
0 Unknown   1 Extractor   2 Model
```

## Validation rules

`comprendo-cli validate` rejects a chart when:

- `schema_version` is missing or unsupported;
- any dataset length disagrees with `N`, `M`, or `K`;
- node ids are not unique, or a cluster id is zero;
- an edge endpoint does not exist;
- `cluster_id` refers to a missing cluster;
- a node kind, edge kind, or provenance value is out of range.

## Versioning

`schema_version` starts at `1`. Additive datasets do not bump it; renames,
removals, or type changes do. The viewer refuses to open a newer major version
and reports the supported range.
