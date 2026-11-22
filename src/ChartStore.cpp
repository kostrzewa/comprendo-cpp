/// \file
/// \brief Reading and writing charts as SQLite databases.

#include "Comprendo/ChartStore.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "ChartSchema.hpp"
#include "Profiler/Profiler.hpp"
#include "Sqlite.hpp"

namespace
{
  using Comprendo::Sqlite::Database;
  using Comprendo::Sqlite::Statement;

  /// Connection settings every chart writer applies before touching the schema.
  constexpr std::string_view kWriterPragmas = "PRAGMA journal_mode = WAL; PRAGMA foreign_keys = "
                                              "ON;";

  /// Suffixes of the files SQLite may keep next to a database.
  constexpr std::array<std::string_view, 3> kSidecarSuffixes = {"-wal", "-shm", "-journal"};

  /// Store an unsigned id in SQLite's signed INTEGER without changing its bits.
  ///
  /// \param id Unsigned id.
  /// \return The same 64 bits as a signed integer.
  std::int64_t
  to_sql_id(std::uint64_t id)
  {
    return std::bit_cast<std::int64_t>(id);
  }

  /// Recover an unsigned id stored with to_sql_id().
  ///
  /// \param id Signed integer read from SQLite.
  /// \return The same 64 bits as an unsigned id.
  std::uint64_t
  from_sql_id(std::int64_t id)
  {
    return std::bit_cast<std::uint64_t>(id);
  }

  /// Append a suffix to a path's file name.
  ///
  /// \param path Base path.
  /// \param suffix Text to append.
  /// \return \p path with \p suffix appended.
  std::filesystem::path
  with_suffix(const std::filesystem::path &path, std::string_view suffix)
  {
    std::filesystem::path result = path;
    result += suffix;
    return result;
  }

  /// Remove SQLite's sidecar files of a database, ignoring files that do not exist.
  ///
  /// \param path Database file whose `-wal`, `-shm`, and `-journal` files are removed.
  void
  remove_sidecars(const std::filesystem::path &path)
  {
    for (const std::string_view suffix : kSidecarSuffixes)
    {
      std::error_code ignored;
      std::filesystem::remove(with_suffix(path, suffix), ignored);
    }
  }

  /// Check that a database is a Comprendo chart with a supported schema version.
  ///
  /// \param db Open connection.
  /// \return The schema version, or an error message.
  std::expected<std::uint32_t, std::string>
  check_identity(Database &db)
  {
    const auto application_id = db.query_int("PRAGMA application_id");
    if (!application_id)
    {
      return std::unexpected(application_id.error());
    }
    if (*application_id != Comprendo::kChartApplicationId)
    {
      return std::unexpected(
        fmt::format(
          "not a Comprendo chart (application_id {:#x}, expected {:#x})",
          *application_id,
          Comprendo::kChartApplicationId
        )
      );
    }
    const auto version = db.query_int("PRAGMA user_version");
    if (!version)
    {
      return std::unexpected(version.error());
    }
    if (*version < 1 || std::cmp_greater(*version, Comprendo::kChartSchemaVersion))
    {
      return std::unexpected(
        fmt::format(
          "unsupported chart schema version {} (supported 1..{})",
          *version,
          Comprendo::kChartSchemaVersion
        )
      );
    }
    return static_cast<std::uint32_t>(*version);
  }

  /// Run an insert statement once per element of a range.
  ///
  /// \param db Open connection.
  /// \param sql Insert statement with positional parameters.
  /// \param rows Elements to insert.
  /// \param bind_row Callable binding one element's values to the statement.
  /// \return Nothing, or the first SQLite error.
  template <typename Rows, typename BindRow>
  std::expected<void, std::string>
  insert_rows(Database &db, std::string_view sql, const Rows &rows, BindRow bind_row)
  {
    auto stmt = Statement::prepare(db, sql);
    if (!stmt)
    {
      return std::unexpected(stmt.error());
    }
    for (const auto &row : rows)
    {
      bind_row(*stmt, row);
      if (auto stepped = stmt->step(); !stepped)
      {
        return std::unexpected(stepped.error());
      }
      stmt->reset();
    }
    return {};
  }

  /// Run a query and call a function for each result row.
  ///
  /// \param db Open connection.
  /// \param sql Select statement.
  /// \param on_row Callable receiving the statement positioned on each row.
  /// \return Nothing, or the first SQLite error.
  template <typename OnRow>
  std::expected<void, std::string>
  for_each_row(Database &db, std::string_view sql, OnRow on_row)
  {
    auto stmt = Statement::prepare(db, sql);
    if (!stmt)
    {
      return std::unexpected(stmt.error());
    }
    while (true)
    {
      const auto row = stmt->step();
      if (!row)
      {
        return std::unexpected(row.error());
      }
      if (!*row)
      {
        return {};
      }
      on_row(*stmt);
    }
  }

  /// Bind an id where 0 means "none" and is stored as NULL.
  ///
  /// \param stmt Statement to bind.
  /// \param index 1-based parameter index.
  /// \param id Id, or 0 for none.
  void
  bind_optional_id(Statement &stmt, int index, std::uint64_t id)
  {
    if (id == 0)
    {
      stmt.bind_null(index);
    }
    else
    {
      stmt.bind(index, to_sql_id(id));
    }
  }

  /// Insert every table's rows of a chart inside the current transaction.
  ///
  /// \param db Connection with an open transaction.
  /// \param chart Chart to insert.
  /// \return Nothing, or the first SQLite error.
  std::expected<void, std::string>
  insert_chart(Database &db, const Comprendo::Chart &chart)
  {
    using namespace Comprendo;

    const std::array<std::pair<std::string_view, std::string_view>, 5> meta = {{
      {"project", chart.project},
      {"git_commit", chart.git_commit},
      {"generated_at", chart.generated_at},
      {"generator", chart.generator},
      {"source_root", chart.source_root},
    }};
    auto inserted = insert_rows(
      db,
      "INSERT INTO meta (key, value) VALUES (?1, ?2)",
      meta,
      [](Statement &stmt, const auto &entry)
      {
        stmt.bind(1, entry.first);
        stmt.bind(2, entry.second);
      }
    );

    inserted = inserted.and_then(
      [&]
      {
        return insert_rows(
          db,
          "INSERT INTO clusters (id, label, summary, color, parent_id) VALUES (?1, ?2, ?3, ?4, ?5)",
          chart.clusters,
          [](Statement &stmt, const Cluster &cluster)
          {
            stmt.bind(1, to_sql_id(cluster.id));
            stmt.bind(2, cluster.label);
            stmt.bind(3, cluster.summary);
            stmt.bind(4, std::int64_t {cluster.color});
            bind_optional_id(stmt, 5, cluster.parent_id);
          }
        );
      }
    );

    inserted = inserted.and_then(
      [&]
      {
        return insert_rows(
          db,
          "INSERT INTO nodes (id, kind, name, qualified_name, file, line_start, line_end, "
          "signature, summary, detail, cluster_id, loc, complexity, provenance) "
          "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)",
          chart.nodes,
          [](Statement &stmt, const Node &node)
          {
            stmt.bind(1, to_sql_id(node.id));
            stmt.bind(2, std::int64_t {std::to_underlying(node.kind)});
            stmt.bind(3, node.name);
            stmt.bind(4, node.qualified_name);
            stmt.bind(5, node.file);
            stmt.bind(6, std::int64_t {node.line_start});
            stmt.bind(7, std::int64_t {node.line_end});
            stmt.bind(8, node.signature);
            stmt.bind(9, node.summary);
            stmt.bind(10, node.detail);
            bind_optional_id(stmt, 11, node.cluster_id);
            stmt.bind(12, std::int64_t {node.loc});
            stmt.bind(13, std::int64_t {node.complexity});
            stmt.bind(14, std::int64_t {std::to_underlying(node.provenance)});
          }
        );
      }
    );

    inserted = inserted.and_then(
      [&]
      {
        return insert_rows(
          db,
          "INSERT INTO edges (src, dst, kind, weight, evidence, provenance) "
          "VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
          chart.edges,
          [](Statement &stmt, const Edge &edge)
          {
            stmt.bind(1, to_sql_id(edge.src));
            stmt.bind(2, to_sql_id(edge.dst));
            stmt.bind(3, std::int64_t {std::to_underlying(edge.kind)});
            stmt.bind(4, static_cast<double>(edge.weight));
            stmt.bind(5, edge.evidence);
            stmt.bind(6, std::int64_t {std::to_underlying(edge.provenance)});
          }
        );
      }
    );

    return inserted.and_then(
      [&]
      {
        return insert_rows(
          db,
          "INSERT INTO layout (node_id, x, y) VALUES (?1, ?2, ?3)",
          std::views::zip(chart.nodes, chart.layout),
          [](Statement &stmt, const auto &row)
          {
            const auto &[node, position] = row;
            stmt.bind(1, to_sql_id(node.id));
            stmt.bind(2, static_cast<double>(position.x));
            stmt.bind(3, static_cast<double>(position.y));
          }
        );
      }
    );
  }

  /// Create a fresh chart database file and fill it.
  ///
  /// The connection is closed on return, which checkpoints and removes the WAL.
  ///
  /// \param path File to create; must not exist.
  /// \param chart Valid chart to write.
  /// \return Nothing, or the first SQLite error.
  std::expected<void, std::string>
  create_database(const std::filesystem::path &path, const Comprendo::Chart &chart)
  {
    auto db = Database::open(path, Comprendo::Sqlite::OpenMode::ReadWriteCreate);
    if (!db)
    {
      return std::unexpected(db.error());
    }
    auto ready = db->exec(kWriterPragmas)
                   .and_then([&] { return db->exec(Comprendo::kChartSchemaSql); })
                   .and_then([&] { return check_identity(*db).transform([](auto) { }); });
    if (!ready)
    {
      return ready;
    }

    auto transaction = Comprendo::Sqlite::Transaction::begin(*db);
    if (!transaction)
    {
      return std::unexpected(transaction.error());
    }
    return db->exec("PRAGMA defer_foreign_keys = ON")
      .and_then([&] { return insert_chart(*db, chart); })
      .and_then([&] { return transaction->commit(); });
  }

  /// Read the `meta` table into the chart's header fields.
  ///
  /// \param db Open connection.
  /// \param chart Chart receiving the values; unknown keys are ignored.
  /// \return Nothing, or the first SQLite error.
  std::expected<void, std::string>
  read_meta(Database &db, Comprendo::Chart &chart)
  {
    const std::unordered_map<std::string_view, std::string *> fields = {
      {"project", &chart.project},
      {"git_commit", &chart.git_commit},
      {"generated_at", &chart.generated_at},
      {"generator", &chart.generator},
      {"source_root", &chart.source_root},
    };
    return for_each_row(
      db,
      "SELECT key, value FROM meta",
      [&fields](const Statement &stmt)
      {
        const auto field = fields.find(stmt.column_text(0));
        if (field != fields.end())
        {
          *field->second = stmt.column_text(1);
        }
      }
    );
  }

  /// Read the node, edge, and cluster tables.
  ///
  /// NULL references read as 0 because Statement::column_int maps NULL to 0.
  ///
  /// \param db Open connection.
  /// \param chart Chart receiving the rows.
  /// \return Nothing, or the first SQLite error.
  std::expected<void, std::string>
  read_graph(Database &db, Comprendo::Chart &chart)
  {
    using namespace Comprendo;

    auto read = for_each_row(
      db,
      "SELECT id, label, summary, color, parent_id FROM clusters",
      [&chart](const Statement &stmt)
      {
        chart.clusters.push_back({
          .id = from_sql_id(stmt.column_int(0)),
          .label = stmt.column_text(1),
          .summary = stmt.column_text(2),
          .color = static_cast<std::uint32_t>(stmt.column_int(3)),
          .parent_id = from_sql_id(stmt.column_int(4)),
        });
      }
    );

    read = read.and_then(
      [&]
      {
        return for_each_row(
          db,
          "SELECT id, kind, name, qualified_name, file, line_start, line_end, signature, "
          "summary, detail, cluster_id, loc, complexity, provenance FROM nodes",
          [&chart](const Statement &stmt)
          {
            chart.nodes.push_back({
              .id = from_sql_id(stmt.column_int(0)),
              .kind = static_cast<NodeKind>(stmt.column_int(1)),
              .name = stmt.column_text(2),
              .qualified_name = stmt.column_text(3),
              .file = stmt.column_text(4),
              .line_start = static_cast<std::uint32_t>(stmt.column_int(5)),
              .line_end = static_cast<std::uint32_t>(stmt.column_int(6)),
              .signature = stmt.column_text(7),
              .summary = stmt.column_text(8),
              .detail = stmt.column_text(9),
              .cluster_id = from_sql_id(stmt.column_int(10)),
              .loc = static_cast<std::uint32_t>(stmt.column_int(11)),
              .complexity = static_cast<std::uint32_t>(stmt.column_int(12)),
              .provenance = static_cast<Provenance>(stmt.column_int(13)),
            });
          }
        );
      }
    );

    return read.and_then(
      [&]
      {
        return for_each_row(
          db,
          "SELECT src, dst, kind, weight, evidence, provenance FROM edges",
          [&chart](const Statement &stmt)
          {
            chart.edges.push_back({
              .src = from_sql_id(stmt.column_int(0)),
              .dst = from_sql_id(stmt.column_int(1)),
              .kind = static_cast<EdgeKind>(stmt.column_int(2)),
              .weight = static_cast<float>(stmt.column_double(3)),
              .evidence = stmt.column_text(4),
              .provenance = static_cast<Provenance>(stmt.column_int(5)),
            });
          }
        );
      }
    );
  }

  /// Read the cached layout, aligned with the chart's node order.
  ///
  /// The layout stays empty unless every node has a position, because a partial
  /// cache has to be recomputed anyway.
  ///
  /// \param db Open connection.
  /// \param chart Chart whose nodes are already loaded.
  /// \return Nothing, or the first SQLite error.
  std::expected<void, std::string>
  read_layout(Database &db, Comprendo::Chart &chart)
  {
    std::unordered_map<std::uint64_t, Comprendo::Position> positions;
    auto read = for_each_row(
      db,
      "SELECT node_id, x, y FROM layout",
      [&positions](const Statement &stmt)
      {
        positions.emplace(
          from_sql_id(stmt.column_int(0)),
          Comprendo::Position {
            .x = static_cast<float>(stmt.column_double(1)),
            .y = static_cast<float>(stmt.column_double(2)),
          }
        );
      }
    );
    if (!read || positions.size() != chart.nodes.size())
    {
      return read;
    }
    chart.layout.reserve(chart.nodes.size());
    for (const Comprendo::Node &node : chart.nodes)
    {
      const auto position = positions.find(node.id);
      if (position == positions.end())
      {
        chart.layout.clear();
        return {};
      }
      chart.layout.push_back(position->second);
    }
    return {};
  }
} // namespace

namespace Comprendo
{
  std::expected<void, std::string>
  write_chart(const std::filesystem::path &path, const Chart &chart)
  {
    const ProfileScope profile {"write_chart"};

    if (const ValidationResult validation = validate(chart); !validation.ok())
    {
      return std::unexpected(
        fmt::format(
          "refusing to write invalid chart to {}: {}",
          path.string(),
          fmt::join(validation.errors, "; ")
        )
      );
    }

    std::error_code error;
    if (path.has_parent_path())
    {
      std::filesystem::create_directories(path.parent_path(), error);
      if (error)
      {
        return std::unexpected(
          fmt::format("cannot create {}: {}", path.parent_path().string(), error.message())
        );
      }
    }

    const std::filesystem::path temp = with_suffix(path, ".tmp");
    std::filesystem::remove(temp, error);
    remove_sidecars(temp);
    if (auto created = create_database(temp, chart); !created)
    {
      std::filesystem::remove(temp, error);
      remove_sidecars(temp);
      return std::unexpected(fmt::format("cannot write {}: {}", path.string(), created.error()));
    }

    remove_sidecars(path);
    std::filesystem::rename(temp, path, error);
    if (error)
    {
      return std::unexpected(
        fmt::format("cannot move {} to {}: {}", temp.string(), path.string(), error.message())
      );
    }
    return {};
  }

  std::expected<Chart, std::string>
  read_chart(const std::filesystem::path &path)
  {
    const ProfileScope profile {"read_chart"};

    if (!std::filesystem::is_regular_file(path))
    {
      return std::unexpected(fmt::format("chart {} does not exist", path.string()));
    }
    auto db = Sqlite::Database::open(path, Sqlite::OpenMode::ReadOnly);
    if (!db)
    {
      return std::unexpected(db.error());
    }

    Chart chart;
    auto read = check_identity(*db)
                  .transform([&chart](std::uint32_t version) { chart.schema_version = version; })
                  .and_then([&] { return read_meta(*db, chart); })
                  .and_then([&] { return read_graph(*db, chart); })
                  .and_then(
                    [&]
                    {
                      canonicalize(chart);
                      return read_layout(*db, chart);
                    }
                  );
    if (!read)
    {
      return std::unexpected(fmt::format("cannot read {}: {}", path.string(), read.error()));
    }
    return chart;
  }
} // namespace Comprendo
