-- Comprendo chart schema, version 1.
--
-- Embedded into comprendo_core via cmake/ChartSchema.hpp.in; every writer (the
-- extractor and the chart tools behind /chart) applies it. See plan/chart-schema.md for the conventions:
-- uint64 ids are stored bit-cast to INTEGER, and NULL means "no cluster".
-- Every statement is idempotent so writers may apply it to an existing chart
-- after checking application_id and user_version.

PRAGMA application_id = 0x434D5052;
PRAGMA user_version = 1;

CREATE TABLE IF NOT EXISTS meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS clusters (
  id INTEGER PRIMARY KEY CHECK (id <> 0),
  label TEXT NOT NULL,
  summary TEXT NOT NULL DEFAULT '',
  color INTEGER NOT NULL DEFAULT 0 CHECK (color BETWEEN 0 AND 16777215),
  parent_id INTEGER REFERENCES clusters (id)
);

CREATE TABLE IF NOT EXISTS nodes (
  id INTEGER PRIMARY KEY,
  kind INTEGER NOT NULL CHECK (kind BETWEEN 0 AND 13),
  name TEXT NOT NULL,
  qualified_name TEXT NOT NULL,
  file TEXT NOT NULL,
  line_start INTEGER NOT NULL DEFAULT 0,
  line_end INTEGER NOT NULL DEFAULT 0,
  signature TEXT NOT NULL DEFAULT '',
  summary TEXT NOT NULL DEFAULT '',
  detail TEXT NOT NULL DEFAULT '',
  cluster_id INTEGER REFERENCES clusters (id) ON DELETE SET NULL,
  loc INTEGER NOT NULL DEFAULT 0,
  complexity INTEGER NOT NULL DEFAULT 0,
  provenance INTEGER NOT NULL CHECK (provenance BETWEEN 0 AND 2)
);

CREATE INDEX IF NOT EXISTS nodes_cluster ON nodes (cluster_id);

CREATE TABLE IF NOT EXISTS edges (
  src INTEGER NOT NULL REFERENCES nodes (id) ON DELETE CASCADE,
  dst INTEGER NOT NULL REFERENCES nodes (id) ON DELETE CASCADE,
  kind INTEGER NOT NULL CHECK (kind BETWEEN 0 AND 6),
  weight REAL NOT NULL DEFAULT 1.0,
  evidence TEXT NOT NULL DEFAULT '',
  provenance INTEGER NOT NULL CHECK (provenance BETWEEN 0 AND 2),
  PRIMARY KEY (src, dst, kind)
) WITHOUT ROWID;

CREATE INDEX IF NOT EXISTS edges_dst ON edges (dst);

CREATE TABLE IF NOT EXISTS layout (
  node_id INTEGER PRIMARY KEY REFERENCES nodes (id) ON DELETE CASCADE,
  x REAL NOT NULL,
  y REAL NOT NULL
);
