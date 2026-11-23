/// \file
/// \brief The chart tools: argument checks, chart access, and the seven handlers.

#include "Comprendo/ChartTools.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "ChartSchema.hpp"
#include "Comprendo/Chart.hpp"
#include "Comprendo/Json.hpp"
#include "Sqlite.hpp"
#include "Version.hpp"

namespace
{
  using Comprendo::Json;
  using Comprendo::Sqlite::Database;
  using Comprendo::Sqlite::Statement;

  /// A tool's result: its output, or an error message for the model.
  using Result = std::expected<std::string, std::string>;

  /// Chart location relative to the project directory.
  constexpr std::string_view kChartPath = ".comprendo/chart.sqlite";

  /// How long a write waits for another writer (the extractor, the viewer's reader).
  constexpr int kBusyTimeoutMs = 5000;

  /// Most nodes one `chart_find_node` call returns.
  constexpr std::int64_t kMaxFindResults = 200;

  /// Results of `chart_find_node` when no limit is given.
  constexpr std::int64_t kDefaultFindResults = 50;

  /// Most files `chart_overview` lists.
  constexpr std::size_t kMaxOverviewFiles = 400;

  /// Longest cluster ancestry walked before a cycle is assumed.
  constexpr int kMaxClusterDepth = 10000;

  /// Node kinds by NodeKind value, as the tools name them.
  constexpr std::array<std::string_view, Comprendo::kNodeKindCount> kNodeKinds = {
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
  };

  /// Edge kinds by EdgeKind value, as the tools name them.
  constexpr std::array<std::string_view, Comprendo::kEdgeKindCount> kEdgeKinds = {
    "other",
    "contains",
    "includes",
    "calls",
    "inherits",
    "uses",
    "data_flow",
  };

  /// NodeKind value of files.
  constexpr std::int64_t kFileKind = 1;

  /// EdgeKind value of containment.
  constexpr std::int64_t kContainsKind = 1;

  /// Provenance value of everything the model writes.
  constexpr std::int64_t kModel = static_cast<std::int64_t>(Comprendo::Provenance::Model);

  /// Saturation of colours derived from cluster labels.
  constexpr double kClusterSaturation = 0.55;

  /// Lightness of colours derived from cluster labels.
  constexpr double kClusterLightness = 0.6;

  /// Columns selected for a NodeRow, in NodeRow order.
  constexpr std::string_view kNodeColumns = "id, kind, name, qualified_name, file, line_start, "
                                            "line_end, signature, summary, detail, "
                                            "coalesce(cluster_id, 0), provenance";

  /// A node as the tools see it.
  struct NodeRow
  {
    /// Node id.
    std::uint64_t id = 0;
    /// NodeKind value.
    std::int64_t kind = 0;
    /// Unqualified name.
    std::string name;
    /// Fully qualified name.
    std::string qualified_name;
    /// Repository-relative file.
    std::string file;
    /// First line, 1-based (0 when unknown).
    std::int64_t line_start = 0;
    /// Last line, 1-based (0 when unknown).
    std::int64_t line_end = 0;
    /// Declaration signature.
    std::string signature;
    /// One-line explanation.
    std::string summary;
    /// Long-form explanation.
    std::string detail;
    /// Owning cluster, or 0 for none.
    std::uint64_t cluster_id = 0;
    /// Provenance value.
    std::int64_t provenance = kModel;
  };

  /// A cluster as the tools see it.
  struct ClusterRow
  {
    /// Cluster id (never 0).
    std::uint64_t id = 0;
    /// Display label.
    std::string label;
    /// One-line explanation.
    std::string summary;
    /// 0xRRGGBB.
    std::int64_t color = 0;
    /// Parent cluster, or 0 for none.
    std::uint64_t parent_id = 0;
  };

  /// Annotation progress of one file.
  struct FileProgress
  {
    /// Repository-relative file.
    std::string file;
    /// Symbols in the file, the file node excluded.
    std::int64_t symbols = 0;
    /// Symbols whose summary came from code comments.
    std::int64_t documented = 0;
    /// Symbols whose summary the model wrote.
    std::int64_t annotated = 0;
    /// Summary of the file node itself.
    std::string summary;
  };

  /// A cluster with its member count.
  struct ClusterSummary
  {
    /// Display label.
    std::string label;
    /// Nodes assigned to it.
    std::int64_t members = 0;
  };

  /// Store an unsigned id in SQLite's signed INTEGER without changing its bits.
  ///
  /// \param id Unsigned id.
  /// \return The same bits as a signed value.
  std::int64_t
  to_sql(std::uint64_t id)
  {
    return static_cast<std::int64_t>(id);
  }

  /// Read an id stored by to_sql().
  ///
  /// \param value Signed value from SQLite.
  /// \return The unsigned id.
  std::uint64_t
  from_sql(std::int64_t value)
  {
    return static_cast<std::uint64_t>(value);
  }

  /// One binding of a query parameter: an integer, a text, or NULL.
  struct Param
  {
    /// Integer value, when not text.
    std::int64_t number = 0;
    /// Text value, when set.
    std::optional<std::string_view> text;
    /// True to bind NULL.
    bool null = false;
  };

  /// Bind an integer parameter.
  ///
  /// \param value Value.
  /// \return The parameter.
  Param
  integer_param(std::int64_t value)
  {
    return {.number = value};
  }

  /// Bind a text parameter.
  ///
  /// \param value Text; must outlive the query.
  /// \return The parameter.
  Param
  text_param(std::string_view value)
  {
    return {.text = value};
  }

  /// Bind an id, or NULL for 0.
  ///
  /// \param id Id; 0 means none.
  /// \return The parameter.
  Param
  optional_id_param(std::uint64_t id)
  {
    return id == 0 ? Param {.null = true} : integer_param(to_sql(id));
  }

  /// Chart access for one tool call. The first SQL failure is kept and later
  /// statements are skipped, so handlers read like plain code and the call
  /// reports the failure (and rolls back) at the end.
  class Store
  {
  public:
    /// Wrap an open chart.
    ///
    /// \param db Connection inside the call's transaction.
    explicit Store(Database &db) : m_db(&db) { }

    /// First SQL failure, if any.
    ///
    /// \return Error message, or nothing.
    [[nodiscard]]
    const std::optional<std::string> &
    error() const
    {
      return m_error;
    }

    /// Run a statement, calling \p on_row for every result row.
    ///
    /// \param sql SQL with `?` parameters.
    /// \param params Parameters in order.
    /// \param on_row Called with the statement positioned on each row.
    void
    query(
      std::string_view sql,
      const std::vector<Param> &params,
      const std::function<void(const Statement &)> &on_row = {}
    )
    {
      if (m_error)
      {
        return;
      }
      auto stmt = Statement::prepare(*m_db, sql);
      if (!stmt)
      {
        m_error = stmt.error();
        return;
      }
      int index = 0;
      for (const Param &param : params)
      {
        ++index;
        if (param.null)
        {
          stmt->bind_null(index);
        }
        else if (const std::optional<std::string_view> &text = param.text)
        {
          stmt->bind(index, *text);
        }
        else
        {
          stmt->bind(index, param.number);
        }
      }
      while (true)
      {
        const auto row = stmt->step();
        if (!row)
        {
          m_error = row.error();
          return;
        }
        if (!*row)
        {
          return;
        }
        if (on_row)
        {
          on_row(*stmt);
        }
      }
    }

    /// Look up a node.
    ///
    /// \param id Node id.
    /// \return The node, or nothing.
    std::optional<NodeRow>
    node(std::uint64_t id)
    {
      std::optional<NodeRow> found;
      query(
        fmt::format("SELECT {} FROM nodes WHERE id = ?", kNodeColumns),
        {integer_param(to_sql(id))},
        [&](const Statement &stmt) { found = read_node(stmt); }
      );
      return found;
    }

    /// Find nodes, best matches first: exact qualified name, exact name, then
    /// qualified names containing the text; ties ordered by file and line.
    ///
    /// \param text Search text; empty for all.
    /// \param file Exact file or directory prefix; empty for all files.
    /// \param kind NodeKind value, or nothing for all kinds.
    /// \param limit Most rows returned.
    /// \return Matching nodes.
    std::vector<NodeRow>
    find_nodes(
      std::string_view text, std::string file, std::optional<std::int64_t> kind, std::int64_t limit
    )
    {
      while (file.ends_with('/'))
      {
        file.pop_back();
      }
      const std::string like = fmt::format("%{}%", escape_like(text));
      const std::string prefix = fmt::format("{}/%", escape_like(file));
      std::vector<NodeRow> nodes;
      query(
        fmt::format(
          "SELECT {} FROM nodes "
          "WHERE (?1 = '' OR qualified_name = ?1 OR name = ?1 OR qualified_name LIKE ?2 ESCAPE "
          "'\\') "
          "AND (?3 = '' OR file = ?3 OR file LIKE ?4 ESCAPE '\\') "
          "AND (?5 IS NULL OR kind = ?5) "
          "ORDER BY CASE WHEN qualified_name = ?1 THEN 0 WHEN name = ?1 THEN 1 ELSE 2 END, "
          "file, line_start, qualified_name LIMIT ?6",
          kNodeColumns
        ),
        {text_param(text),
         text_param(like),
         text_param(file),
         text_param(prefix),
         kind ? integer_param(*kind) : Param {.null = true},
         integer_param(limit)},
        [&](const Statement &stmt) { nodes.push_back(read_node(stmt)); }
      );
      return nodes;
    }

    /// Insert a node. Stale layout rows are removed so the viewer lays out
    /// the changed chart again.
    ///
    /// \param node Node with a new id.
    void
    insert_node(const NodeRow &node)
    {
      query(
        "INSERT INTO nodes (id, kind, name, qualified_name, file, line_start, line_end, "
        "signature, summary, detail, cluster_id, provenance) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {integer_param(to_sql(node.id)),
         integer_param(node.kind),
         text_param(node.name),
         text_param(node.qualified_name),
         text_param(node.file),
         integer_param(node.line_start),
         integer_param(node.line_end),
         text_param(node.signature),
         text_param(node.summary),
         text_param(node.detail),
         optional_id_param(node.cluster_id),
         integer_param(node.provenance)}
      );
      query("DELETE FROM layout", {});
    }

    /// Set a node's explanation; empty values keep the current text. The
    /// node's provenance becomes Model, since its annotation now comes from
    /// the model.
    ///
    /// \param id Node id.
    /// \param summary New summary, or empty.
    /// \param detail New detail, or empty.
    void
    annotate_node(std::uint64_t id, std::string_view summary, std::string_view detail)
    {
      query(
        "UPDATE nodes SET summary = CASE WHEN ?1 = '' THEN summary ELSE ?1 END, "
        "detail = CASE WHEN ?2 = '' THEN detail ELSE ?2 END, provenance = ?3 WHERE id = ?4",
        {text_param(summary), text_param(detail), integer_param(kModel), integer_param(to_sql(id))}
      );
    }

    /// Insert or update an edge. An existing edge keeps its provenance and,
    /// when \p evidence is empty, its evidence.
    ///
    /// \param src Source node id.
    /// \param dst Destination node id.
    /// \param kind EdgeKind value.
    /// \param weight Edge weight.
    /// \param evidence Where the relationship shows.
    /// \return True when the edge was new.
    bool
    upsert_edge(
      std::uint64_t src,
      std::uint64_t dst,
      std::int64_t kind,
      std::int64_t weight,
      std::string_view evidence
    )
    {
      bool existed = false;
      query(
        "SELECT 1 FROM edges WHERE src = ? AND dst = ? AND kind = ?",
        {integer_param(to_sql(src)), integer_param(to_sql(dst)), integer_param(kind)},
        [&](const Statement &) { existed = true; }
      );
      query(
        "INSERT INTO edges (src, dst, kind, weight, evidence, provenance) "
        "VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT (src, dst, kind) DO UPDATE SET "
        "weight = excluded.weight, "
        "evidence = CASE WHEN excluded.evidence = '' THEN edges.evidence ELSE excluded.evidence "
        "END",
        {integer_param(to_sql(src)),
         integer_param(to_sql(dst)),
         integer_param(kind),
         integer_param(weight),
         text_param(evidence),
         integer_param(kModel)}
      );
      return !existed;
    }

    /// Look up a cluster.
    ///
    /// \param id Cluster id.
    /// \return The cluster, or nothing.
    std::optional<ClusterRow>
    cluster(std::uint64_t id)
    {
      std::optional<ClusterRow> found;
      query(
        "SELECT id, label, summary, color, coalesce(parent_id, 0) FROM clusters WHERE id = ?",
        {integer_param(to_sql(id))},
        [&](const Statement &stmt)
        {
          found = ClusterRow {
            .id = from_sql(stmt.column_int(0)),
            .label = stmt.column_text(1),
            .summary = stmt.column_text(2),
            .color = stmt.column_int(3),
            .parent_id = from_sql(stmt.column_int(4)),
          };
        }
      );
      return found;
    }

    /// Insert or replace a cluster's fields.
    ///
    /// \param cluster Cluster to write.
    void
    upsert_cluster(const ClusterRow &cluster)
    {
      query(
        "INSERT INTO clusters (id, label, summary, color, parent_id) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT (id) DO UPDATE SET label = excluded.label, summary = excluded.summary, "
        "color = excluded.color, parent_id = excluded.parent_id",
        {integer_param(to_sql(cluster.id)),
         text_param(cluster.label),
         text_param(cluster.summary),
         integer_param(cluster.color),
         optional_id_param(cluster.parent_id)}
      );
    }

    /// Check whether making \p parent the parent of \p id would form a cycle.
    ///
    /// \param id Cluster id.
    /// \param parent Proposed parent.
    /// \return True when \p id is \p parent or one of its ancestors.
    bool
    is_cluster_ancestor(std::uint64_t id, std::uint64_t parent)
    {
      std::uint64_t current = parent;
      for (int steps = 0; current != 0; ++steps)
      {
        if (current == id || steps > kMaxClusterDepth)
        {
          return true;
        }
        const auto row = cluster(current);
        current = row ? row->parent_id : 0;
      }
      return false;
    }

    /// Assign a node to a cluster.
    ///
    /// \param node Node id.
    /// \param cluster Cluster id.
    void
    set_node_cluster(std::uint64_t node, std::uint64_t cluster)
    {
      query(
        "UPDATE nodes SET cluster_id = ? WHERE id = ?",
        {optional_id_param(cluster), integer_param(to_sql(node))}
      );
    }

    /// Read a meta value.
    ///
    /// \param key Key.
    /// \return The value, or empty.
    std::string
    meta(std::string_view key)
    {
      std::string value;
      query(
        "SELECT value FROM meta WHERE key = ?",
        {text_param(key)},
        [&](const Statement &stmt) { value = stmt.column_text(0); }
      );
      return value;
    }

    /// Write a meta value.
    ///
    /// \param key Key.
    /// \param value Value.
    void
    set_meta(std::string_view key, std::string_view value)
    {
      query(
        "INSERT INTO meta (key, value) VALUES (?, ?) "
        "ON CONFLICT (key) DO UPDATE SET value = excluded.value",
        {text_param(key), text_param(value)}
      );
    }

    /// Count rows by kind.
    ///
    /// \param table `nodes` or `edges`.
    /// \return (kind, count) pairs ordered by kind.
    std::vector<std::pair<std::int64_t, std::int64_t>>
    count_by_kind(std::string_view table)
    {
      std::vector<std::pair<std::int64_t, std::int64_t>> counts;
      query(
        fmt::format("SELECT kind, count(*) FROM {} GROUP BY kind ORDER BY kind", table),
        {},
        [&](const Statement &stmt) { counts.emplace_back(stmt.column_int(0), stmt.column_int(1)); }
      );
      return counts;
    }

    /// Summarize the chart per file.
    ///
    /// \return One entry per file, ordered by path.
    std::vector<FileProgress>
    files()
    {
      std::vector<FileProgress> files;
      query(
        "SELECT file, sum(kind <> 1), sum(kind <> 1 AND summary <> '' AND provenance <> ?1), "
        "sum(kind <> 1 AND summary <> '' AND provenance = ?1), "
        "coalesce(max(CASE WHEN kind = 1 THEN summary END), '') "
        "FROM nodes GROUP BY file ORDER BY file",
        {integer_param(kModel)},
        [&](const Statement &stmt)
        {
          files.push_back({
            .file = stmt.column_text(0),
            .symbols = stmt.column_int(1),
            .documented = stmt.column_int(2),
            .annotated = stmt.column_int(3),
            .summary = stmt.column_text(4),
          });
        }
      );
      return files;
    }

    /// List the clusters with their member counts.
    ///
    /// \return Clusters ordered by label.
    std::vector<ClusterSummary>
    clusters()
    {
      std::vector<ClusterSummary> clusters;
      query(
        "SELECT c.label, (SELECT count(*) FROM nodes n WHERE n.cluster_id = c.id) "
        "FROM clusters c ORDER BY c.label",
        {},
        [&](const Statement &stmt)
        { clusters.push_back({.label = stmt.column_text(0), .members = stmt.column_int(1)}); }
      );
      return clusters;
    }

    /// Run SQLite's integrity and foreign key checks.
    ///
    /// \return Problems found; empty when the chart is sound.
    std::vector<std::string>
    check()
    {
      std::vector<std::string> problems;
      query(
        "PRAGMA integrity_check",
        {},
        [&](const Statement &stmt)
        {
          if (std::string message = stmt.column_text(0); message != "ok")
          {
            problems.push_back(std::move(message));
          }
        }
      );
      query(
        "PRAGMA foreign_key_check",
        {},
        [&](const Statement &stmt)
        {
          problems.push_back(
            fmt::format(
              "foreign key violation in {} row {}", stmt.column_text(0), stmt.column_int(1)
            )
          );
        }
      );
      return problems;
    }

  private:
    /// Read a node selected with kNodeColumns.
    ///
    /// \param stmt Statement positioned on a row.
    /// \return The node.
    static NodeRow
    read_node(const Statement &stmt)
    {
      return {
        .id = from_sql(stmt.column_int(0)),
        .kind = stmt.column_int(1),
        .name = stmt.column_text(2),
        .qualified_name = stmt.column_text(3),
        .file = stmt.column_text(4),
        .line_start = stmt.column_int(5),
        .line_end = stmt.column_int(6),
        .signature = stmt.column_text(7),
        .summary = stmt.column_text(8),
        .detail = stmt.column_text(9),
        .cluster_id = from_sql(stmt.column_int(10)),
        .provenance = stmt.column_int(11),
      };
    }

    /// Escape `%`, `_`, and `\` for a `LIKE ... ESCAPE '\'` pattern.
    ///
    /// \param text Literal text.
    /// \return Escaped text.
    static std::string
    escape_like(std::string_view text)
    {
      std::string escaped;
      for (const char c : text)
      {
        if (c == '\\' || c == '%' || c == '_')
        {
          escaped.push_back('\\');
        }
        escaped.push_back(c);
      }
      return escaped;
    }

    /// Connection.
    Database *m_db;
    /// First SQL failure.
    std::optional<std::string> m_error;
  };

  /// Remove leading and trailing whitespace.
  ///
  /// \param text Text.
  /// \return Trimmed view into \p text.
  std::string_view
  trim(std::string_view text)
  {
    constexpr std::string_view kSpace = " \t\r\n\f\v";
    const std::size_t first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos)
    {
      return {};
    }
    return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
  }

  /// Lower-case ASCII text.
  ///
  /// \param text Text.
  /// \return Lower-cased copy.
  std::string
  lower(std::string_view text)
  {
    std::string result {text};
    std::ranges::transform(
      result,
      result.begin(),
      [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
    );
    return result;
  }

  /// Look up a kind by its tool name.
  ///
  /// \param names Kind names by value.
  /// \param name Name from the model; case and surrounding whitespace ignored.
  /// \return Kind value, or nothing.
  template <std::size_t N>
  std::optional<std::int64_t>
  kind_value(const std::array<std::string_view, N> &names, std::string_view name)
  {
    const auto found = std::ranges::find(names, lower(trim(name)));
    if (found == names.end())
    {
      return std::nullopt;
    }
    return static_cast<std::int64_t>(found - names.begin());
  }

  /// Name of a node kind value.
  ///
  /// \param kind NodeKind value.
  /// \return Tool name of the kind.
  std::string_view
  node_kind_name(std::int64_t kind)
  {
    return kind >= 0 && std::cmp_less(kind, kNodeKinds.size())
      ? kNodeKinds.at(static_cast<std::size_t>(kind))
      : "other";
  }

  /// Join kind names for messages.
  ///
  /// \param names Kind names.
  /// \return Comma-separated names.
  template <std::size_t N>
  std::string
  kind_list(const std::array<std::string_view, N> &names)
  {
    return fmt::format("{}", fmt::join(names, ", "));
  }

  /// Read a string argument; missing or null counts as empty.
  ///
  /// \param args Arguments.
  /// \param key Argument name.
  /// \param required True when an empty value is an error.
  /// \return The trimmed string, or an error.
  Result
  text_arg(const Json &args, std::string_view key, bool required = false)
  {
    const Json *value = args.find(key);
    std::string_view text;
    if (value != nullptr && !value->is_null())
    {
      const std::string *string = value->as_string();
      if (string == nullptr)
      {
        return std::unexpected(fmt::format("{} must be a string", key));
      }
      text = trim(*string);
    }
    if (required && text.empty())
    {
      return std::unexpected(fmt::format("{} is required", key));
    }
    return std::string {text};
  }

  /// Read a non-negative integer argument; missing, null, or empty counts as
  /// the fallback, and digits in a string are accepted.
  ///
  /// \param args Arguments.
  /// \param key Argument name.
  /// \param fallback Value when absent.
  /// \return The integer, or an error.
  std::expected<std::int64_t, std::string>
  integer_arg(const Json &args, std::string_view key, std::int64_t fallback)
  {
    const Json *value = args.find(key);
    const auto invalid
      = [&] { return std::unexpected(fmt::format("{} must be a non-negative integer", key)); };
    if (value == nullptr || value->is_null())
    {
      return fallback;
    }
    if (const std::string *string = value->as_string())
    {
      const std::string_view digits = trim(*string);
      if (digits.empty())
      {
        return fallback;
      }
      std::int64_t parsed = 0;
      const auto [end, error] = std::from_chars(digits.begin(), digits.end(), parsed);
      if (error != std::errc {} || end != digits.end() || parsed < 0)
      {
        return invalid();
      }
      return parsed;
    }
    const double *number = value->as_number();
    constexpr double kLargest = 9.0e15;
    if (number == nullptr || *number < 0.0 || *number > kLargest || std::trunc(*number) != *number)
    {
      return invalid();
    }
    return static_cast<std::int64_t>(*number);
  }

  /// Read a list of strings; a single string is split at commas and whitespace.
  ///
  /// \param args Arguments.
  /// \param key Argument name.
  /// \return Non-empty entries, or an error.
  std::expected<std::vector<std::string>, std::string>
  list_arg(const Json &args, std::string_view key)
  {
    const Json *value = args.find(key);
    std::vector<std::string> items;
    if (value == nullptr || value->is_null())
    {
      return items;
    }
    if (const std::string *string = value->as_string())
    {
      std::string current;
      for (const char c : *string)
      {
        if (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
          if (!current.empty())
          {
            items.push_back(std::exchange(current, {}));
          }
        }
        else
        {
          current.push_back(c);
        }
      }
      if (!current.empty())
      {
        items.push_back(current);
      }
      return items;
    }
    const Json::Array *array = value->as_array();
    if (array == nullptr)
    {
      return std::unexpected(fmt::format("{} must be a list of strings", key));
    }
    for (const Json &item : *array)
    {
      const std::string *string = item.as_string();
      if (string == nullptr)
      {
        return std::unexpected(fmt::format("{} must be a list of strings", key));
      }
      if (const std::string_view trimmed = trim(*string); !trimmed.empty())
      {
        items.emplace_back(trimmed);
      }
    }
    return items;
  }

  /// Resolve a node id text and check the node exists.
  ///
  /// \param store Chart.
  /// \param raw Id text.
  /// \param key Argument name, for messages.
  /// \return The node, or an error.
  std::expected<NodeRow, std::string>
  existing_node(Store &store, std::string_view raw, std::string_view key)
  {
    const auto id = Comprendo::parse_id(raw);
    if (!id)
    {
      return std::unexpected(
        fmt::format("{} '{}' is not a node id (use the 0x… ids chart_find_node prints)", key, raw)
      );
    }
    auto node = store.node(*id);
    if (!node)
    {
      return std::unexpected(
        fmt::format(
          "no node with id {}; look it up with chart_find_node", Comprendo::format_id(*id)
        )
      );
    }
    return std::move(*node);
  }

  /// Read a node id argument and check the node exists.
  ///
  /// \param store Chart.
  /// \param args Arguments.
  /// \param key Argument name.
  /// \return The node, or an error.
  std::expected<NodeRow, std::string>
  node_arg(Store &store, const Json &args, std::string_view key)
  {
    return text_arg(args, key, true)
      .and_then([&](const std::string &raw) { return existing_node(store, raw, key); });
  }

  /// Normalize a path argument to a repository-relative path with forward slashes.
  ///
  /// \param path Path from the model.
  /// \param root Project directory, for absolute paths.
  /// \return Relative path, or an error when it leaves the project.
  Result
  repository_path(std::string_view path, const std::filesystem::path &root)
  {
    std::string text {path};
    std::ranges::replace(text, '\\', '/');
    const std::filesystem::path given {text};
    const std::filesystem::path relative = given.is_absolute()
      ? given.lexically_normal().lexically_relative(root.lexically_normal())
      : given.lexically_normal();
    std::string result = relative.generic_string();
    if (result.starts_with("./"))
    {
      result.erase(0, 2);
    }
    if (result.empty() || result == "." || result == ".." || result.starts_with("../"))
    {
      return std::unexpected(fmt::format("file '{}' is outside the project", path));
    }
    return result;
  }

  /// Unqualified name: the part after the last `::` or `.`.
  ///
  /// \param qualified_name Qualified name.
  /// \return Name.
  std::string
  last_segment(std::string_view qualified_name)
  {
    const std::size_t colons = qualified_name.rfind("::");
    const std::size_t dot = qualified_name.rfind('.');
    std::size_t start = 0;
    if (colons != std::string_view::npos)
    {
      start = colons + 2;
    }
    if (dot != std::string_view::npos && dot + 1 > start)
    {
      start = dot + 1;
    }
    return std::string {qualified_name.substr(start)};
  }

  /// Convert HSL to a packed RGB value.
  ///
  /// \param hue Hue in degrees.
  /// \param saturation Saturation in [0, 1].
  /// \param lightness Lightness in [0, 1].
  /// \return 0xRRGGBB.
  std::int64_t
  hsl_to_rgb(double hue, double saturation, double lightness)
  {
    constexpr double kSector = 30.0;
    constexpr double kHueSteps = 12.0;
    constexpr double kChannelMax = 255.0;
    const double chroma = (1.0 - std::abs((2.0 * lightness) - 1.0)) * saturation;
    const auto channel = [&](double n)
    {
      const double k = std::fmod(n + (hue / kSector), kHueSteps);
      const double value
        = lightness - (chroma / 2.0 * std::max(-1.0, std::min({k - 3.0, 9.0 - k, 1.0})));
      return static_cast<std::int64_t>(std::floor((value * kChannelMax) + 0.5));
    };
    constexpr double kRed = 0.0;
    constexpr double kGreen = 8.0;
    constexpr double kBlue = 4.0;
    constexpr int kRedShift = 16;
    constexpr int kGreenShift = 8;
    return (channel(kRed) << kRedShift) | (channel(kGreen) << kGreenShift) | channel(kBlue);
  }

  /// Parse a `#RRGGBB` colour; empty derives one from the label.
  ///
  /// \param color Colour text.
  /// \param label Cluster label.
  /// \return 0xRRGGBB, or an error.
  std::expected<std::int64_t, std::string>
  parse_color(std::string_view color, std::string_view label)
  {
    constexpr std::uint64_t kHues = 360;
    constexpr std::size_t kHexDigits = 6;
    constexpr int kHexBase = 16;
    if (color.empty())
    {
      const auto hue = static_cast<double>(Comprendo::make_cluster_id(label) % kHues);
      return hsl_to_rgb(hue, kClusterSaturation, kClusterLightness);
    }
    std::string_view hex = color;
    if (hex.starts_with('#'))
    {
      hex.remove_prefix(1);
    }
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(hex.begin(), hex.end(), value, kHexBase);
    if (
      hex.size() != kHexDigits || error != std::errc {} || end != hex.end()
      || !std::ranges::all_of(
        hex, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }
      )
    )
    {
      return std::unexpected(fmt::format("color '{}' must look like #4a90d9", color));
    }
    return value;
  }

  /// Describe a node in one line for tool output.
  ///
  /// \param node Node.
  /// \return `id kind qualified_name signature file:start-end`, plus the
  ///         summary, marked `[doc]` when it came from code comments.
  std::string
  describe(const NodeRow &node)
  {
    const std::string lines
      = node.line_start > 0 ? fmt::format(":{}-{}", node.line_start, node.line_end) : "";
    const std::string signature = node.signature.empty() ? "" : " " + node.signature;
    const std::string_view source = node.provenance == kModel ? "" : "[doc] ";
    const std::string summary
      = node.summary.empty() ? "" : fmt::format(" — {}{}", source, node.summary);
    return fmt::format(
      "{} {} {}{} {}{}{}",
      Comprendo::format_id(node.id),
      node_kind_name(node.kind),
      node.qualified_name,
      signature,
      node.file,
      lines,
      summary
    );
  }

  /// Inputs of one tool call.
  class Call
  {
  public:
    /// Bundle the inputs; all must outlive the call.
    ///
    /// \param store Chart.
    /// \param args Argument object.
    /// \param root Project directory.
    /// \param chart Chart file.
    Call(
      Store &store,
      const Json &args,
      const std::filesystem::path &root,
      const std::filesystem::path &chart
    ) : m_store(&store), m_args(&args), m_root(&root), m_chart(&chart)
    {
    }

    /// Chart.
    ///
    /// \return Store of the call.
    [[nodiscard]]
    Store &
    store() const
    {
      return *m_store;
    }

    /// Argument object.
    ///
    /// \return Arguments from the model.
    [[nodiscard]]
    const Json &
    args() const
    {
      return *m_args;
    }

    /// Project directory.
    ///
    /// \return Absolute project directory.
    [[nodiscard]]
    const std::filesystem::path &
    root() const
    {
      return *m_root;
    }

    /// Chart file.
    ///
    /// \return Path of the chart.
    [[nodiscard]]
    const std::filesystem::path &
    chart() const
    {
      return *m_chart;
    }

  private:
    /// Chart.
    Store *m_store;
    /// Argument object.
    const Json *m_args;
    /// Project directory.
    const std::filesystem::path *m_root;
    /// Chart file.
    const std::filesystem::path *m_chart;
  };

  /// A tool handler.
  using Handler = Result (*)(Call &);

  /// `chart_overview`: counts, clusters, and per-file progress.
  ///
  /// \param call Call inputs.
  /// \return Overview text.
  Result
  chart_overview(Call &call)
  {
    Store &store = call.store();
    const auto counts = [](const auto &pairs, const auto &names)
    {
      std::vector<std::string> parts;
      for (const auto &[kind, count] : pairs)
      {
        const bool known = kind >= 0 && std::cmp_less(kind, names.size());
        parts.push_back(
          fmt::format(
            "{} {}",
            known ? std::string {names.at(static_cast<std::size_t>(kind))} : std::to_string(kind),
            count
          )
        );
      }
      return parts.empty() ? std::string {"none"} : fmt::format("{}", fmt::join(parts, ", "));
    };
    std::vector<std::string> cluster_texts;
    for (const ClusterSummary &cluster : store.clusters())
    {
      cluster_texts.push_back(fmt::format("{} ({})", cluster.label, cluster.members));
    }
    const std::string generator = store.meta("generator");
    std::vector<std::string> lines = {
      fmt::format("chart: {}", call.chart().string()),
      fmt::format("generator: {}", generator.empty() ? "(none)" : generator),
      fmt::format("nodes: {}", counts(store.count_by_kind("nodes"), kNodeKinds)),
      fmt::format("edges: {}", counts(store.count_by_kind("edges"), kEdgeKinds)),
      fmt::format(
        "clusters: {}",
        cluster_texts.empty() ? std::string {"none"}
                              : fmt::format("{}", fmt::join(cluster_texts, ", "))
      ),
      "",
      "files (symbols, summaries from code docs, summaries by you, file summary):",
    };
    const std::vector<FileProgress> files = store.files();
    for (std::size_t i = 0; i < std::min(files.size(), kMaxOverviewFiles); ++i)
    {
      const FileProgress &file = files[i];
      lines.push_back(
        fmt::format(
          "  {}  {} {} {}{}",
          file.file,
          file.symbols,
          file.documented,
          file.annotated,
          file.summary.empty() ? "" : "  — " + file.summary
        )
      );
    }
    if (files.size() > kMaxOverviewFiles)
    {
      lines.push_back(
        fmt::format(
          "  … {} more files; use chart_find_node with file", files.size() - kMaxOverviewFiles
        )
      );
    }
    if (files.empty())
    {
      lines.emplace_back(
        "  (empty: run `comprendo-cli extract`, or add nodes with chart_add_node)"
      );
    }
    return fmt::format("{}", fmt::join(lines, "\n"));
  }

  /// `chart_find_node`: look nodes up by name, file, and kind.
  ///
  /// \param call Call inputs.
  /// \return One line per node, or an error.
  Result
  chart_find_node(Call &call)
  {
    const auto kind_text = text_arg(call.args(), "kind");
    const auto file = text_arg(call.args(), "file");
    const auto query = text_arg(call.args(), "query");
    const auto requested = integer_arg(call.args(), "limit", kDefaultFindResults);
    if (!kind_text || !file || !query || !requested)
    {
      return std::unexpected(
        !kind_text ? kind_text.error()
          : !file  ? file.error()
          : !query ? query.error()
                   : requested.error()
      );
    }
    const auto kind = kind_text->empty() ? std::nullopt : kind_value(kNodeKinds, *kind_text);
    if (!kind_text->empty() && !kind)
    {
      return std::unexpected(
        fmt::format("unknown kind '{}'; use one of {}", *kind_text, kind_list(kNodeKinds))
      );
    }
    std::string folder;
    if (!file->empty())
    {
      auto relative = repository_path(*file, call.root());
      if (!relative)
      {
        return relative;
      }
      folder = std::move(*relative);
    }
    const std::int64_t limit = std::clamp<std::int64_t>(*requested, 1, kMaxFindResults);
    const std::vector<NodeRow> nodes = call.store().find_nodes(*query, folder, kind, limit + 1);
    if (nodes.empty())
    {
      return std::string {"no matching nodes"};
    }
    std::vector<std::string> lines;
    for (std::size_t i = 0; std::cmp_less(i, std::min<std::int64_t>(limit, std::ssize(nodes))); ++i)
    {
      lines.push_back(describe(nodes[i]));
    }
    if (std::ssize(nodes) > limit)
    {
      lines.emplace_back("… more matches; narrow the query or raise limit");
    }
    return fmt::format("{}", fmt::join(lines, "\n"));
  }

  /// Arguments of `chart_add_node` after validation.
  struct NewNode
  {
    /// NodeKind value.
    std::int64_t kind = 0;
    /// Repository-relative file.
    std::string file;
    /// Qualified name (the file path for files).
    std::string qualified_name;
    /// Signature.
    std::string signature;
    /// First line.
    std::int64_t line_start = 0;
    /// Last line, at least line_start.
    std::int64_t line_end = 0;
    /// Summary.
    std::string summary;
    /// Detail.
    std::string detail;
    /// Parent id text, or empty for the file.
    std::string parent;
  };

  /// Validate the arguments of `chart_add_node`.
  ///
  /// \param call Call inputs.
  /// \return The node's fields, or an error.
  std::expected<NewNode, std::string>
  new_node(Call &call)
  {
    const auto kind_text = text_arg(call.args(), "kind", true);
    if (!kind_text)
    {
      return std::unexpected(kind_text.error());
    }
    const auto kind = kind_value(kNodeKinds, *kind_text);
    if (!kind)
    {
      return std::unexpected(
        fmt::format("unknown kind '{}'; use one of {}", *kind_text, kind_list(kNodeKinds))
      );
    }
    const auto file
      = text_arg(call.args(), "file", true)
          .and_then([&](const std::string &path) { return repository_path(path, call.root()); });
    if (!file)
    {
      return std::unexpected(file.error());
    }
    const auto qualified_name
      = *kind == kFileKind ? Result {*file} : text_arg(call.args(), "qualified_name", true);
    const auto signature = text_arg(call.args(), "signature");
    const auto line_start = integer_arg(call.args(), "line_start", 0);
    if (!qualified_name || !signature || !line_start)
    {
      return std::unexpected(
        !qualified_name ? qualified_name.error()
          : !signature  ? signature.error()
                        : line_start.error()
      );
    }
    const auto line_end = integer_arg(call.args(), "line_end", *line_start);
    const auto summary = text_arg(call.args(), "summary");
    const auto detail = text_arg(call.args(), "detail");
    const auto parent = text_arg(call.args(), "parent");
    if (!line_end || !summary || !detail || !parent)
    {
      return std::unexpected(
        !line_end    ? line_end.error()
          : !summary ? summary.error()
          : !detail  ? detail.error()
                     : parent.error()
      );
    }
    return NewNode {
      .kind = *kind,
      .file = *file,
      .qualified_name = *qualified_name,
      .signature = *signature,
      .line_start = *line_start,
      .line_end = std::max(*line_end, *line_start),
      .summary = *summary,
      .detail = *detail,
      .parent = *parent,
    };
  }

  /// Make a node id from tool fields.
  ///
  /// \param kind NodeKind value.
  /// \param qualified_name Qualified name.
  /// \param file Repository-relative file.
  /// \param signature Signature.
  /// \return Id, as the extractor computes it.
  std::uint64_t
  node_id(
    std::int64_t kind,
    std::string_view qualified_name,
    std::string_view file,
    std::string_view signature
  )
  {
    return Comprendo::make_node_id(
      static_cast<Comprendo::NodeKind>(kind), qualified_name, file, signature
    );
  }

  /// `chart_add_node`: add a symbol under its parent or its file.
  ///
  /// \param call Call inputs.
  /// \return What was added, or an error.
  Result
  chart_add_node(Call &call)
  {
    const auto fields = new_node(call);
    if (!fields)
    {
      return std::unexpected(fields.error());
    }
    Store &store = call.store();
    const std::uint64_t id
      = node_id(fields->kind, fields->qualified_name, fields->file, fields->signature);
    if (const auto existing = store.node(id))
    {
      return fmt::format("already in the chart: {}", describe(*existing));
    }
    std::optional<NodeRow> parent;
    if (!fields->parent.empty())
    {
      auto found = existing_node(store, fields->parent, "parent");
      if (!found)
      {
        return std::unexpected(found.error());
      }
      parent = std::move(*found);
    }
    else if (fields->kind != kFileKind)
    {
      const std::uint64_t file_id = node_id(kFileKind, fields->file, fields->file, "");
      parent = store.node(file_id);
      if (!parent)
      {
        parent = NodeRow {
          .id = file_id,
          .kind = kFileKind,
          .name = std::filesystem::path {fields->file}.filename().string(),
          .qualified_name = fields->file,
          .file = fields->file,
        };
        store.insert_node(*parent);
      }
    }
    const NodeRow node {
      .id = id,
      .kind = fields->kind,
      .name = fields->kind == kFileKind ? std::filesystem::path {fields->file}.filename().string()
                                        : last_segment(fields->qualified_name),
      .qualified_name = fields->qualified_name,
      .file = fields->file,
      .line_start = fields->line_start,
      .line_end = fields->line_end,
      .signature = fields->signature,
      .summary = fields->summary,
      .detail = fields->detail,
    };
    store.insert_node(node);
    if (parent)
    {
      store.upsert_edge(parent->id, id, kContainsKind, 1, "");
    }
    return fmt::format("added {}", describe(node));
  }

  /// `chart_annotate_node`: set a node's summary and detail.
  ///
  /// \param call Call inputs.
  /// \return Confirmation, or an error.
  Result
  chart_annotate_node(Call &call)
  {
    const auto node = node_arg(call.store(), call.args(), "id");
    const auto summary = text_arg(call.args(), "summary");
    const auto detail = text_arg(call.args(), "detail");
    if (!node || !summary || !detail)
    {
      return std::unexpected(!node ? node.error() : !summary ? summary.error() : detail.error());
    }
    if (summary->empty() && detail->empty())
    {
      return std::unexpected(std::string {"give a summary, a detail, or both"});
    }
    call.store().annotate_node(node->id, *summary, *detail);
    return fmt::format("annotated {}", node->qualified_name);
  }

  /// `chart_annotate_cluster`: create or update a cluster and assign members.
  ///
  /// \param call Call inputs.
  /// \return Confirmation, or an error.
  Result
  chart_annotate_cluster(Call &call)
  {
    Store &store = call.store();
    const auto label = text_arg(call.args(), "label", true);
    const auto parent_label = text_arg(call.args(), "parent_label");
    const auto summary = text_arg(call.args(), "summary");
    const auto color_text = text_arg(call.args(), "color");
    const auto members = list_arg(call.args(), "members");
    if (!label || !parent_label || !summary || !color_text || !members)
    {
      return std::unexpected(
        !label            ? label.error()
          : !parent_label ? parent_label.error()
          : !summary      ? summary.error()
          : !color_text   ? color_text.error()
                          : members.error()
      );
    }
    const std::uint64_t id = Comprendo::make_cluster_id(*label);
    const std::uint64_t parent_id
      = parent_label->empty() ? 0 : Comprendo::make_cluster_id(*parent_label);
    if (parent_id != 0 && !store.cluster(parent_id))
    {
      return std::unexpected(
        fmt::format("no cluster labelled '{}'; create it first", *parent_label)
      );
    }
    if (parent_id != 0 && store.is_cluster_ancestor(id, parent_id))
    {
      return std::unexpected(
        fmt::format("'{}' cannot enclose '{}': clusters would form a cycle", *parent_label, *label)
      );
    }
    const std::optional<ClusterRow> existing = store.cluster(id);
    std::int64_t color = 0;
    if (color_text->empty() && existing)
    {
      color = existing->color;
    }
    else
    {
      const auto parsed = parse_color(*color_text, *label);
      if (!parsed)
      {
        return std::unexpected(parsed.error());
      }
      color = *parsed;
    }
    store.upsert_cluster({
      .id = id,
      .label = *label,
      .summary = summary->empty() && existing ? existing->summary : *summary,
      .color = color,
      .parent_id = parent_id,
    });
    std::vector<std::uint64_t> member_ids;
    for (const std::string &raw : *members)
    {
      const auto node = existing_node(store, raw, "id");
      if (!node)
      {
        return std::unexpected(node.error());
      }
      member_ids.push_back(node->id);
    }
    for (const std::uint64_t member : member_ids)
    {
      store.set_node_cluster(member, id);
    }
    return fmt::format(
      "{} cluster {} ({}); assigned {} nodes",
      existing ? "updated" : "created",
      *label,
      Comprendo::format_id(id),
      member_ids.size()
    );
  }

  /// `chart_add_edge`: record a relationship.
  ///
  /// \param call Call inputs.
  /// \return Confirmation, or an error.
  Result
  chart_add_edge(Call &call)
  {
    Store &store = call.store();
    const auto src = node_arg(store, call.args(), "src");
    const auto dst = node_arg(store, call.args(), "dst");
    const auto kind_text = text_arg(call.args(), "kind", true);
    if (!src || !dst || !kind_text)
    {
      return std::unexpected(!src ? src.error() : !dst ? dst.error() : kind_text.error());
    }
    const auto kind = kind_value(kEdgeKinds, *kind_text);
    if (!kind)
    {
      return std::unexpected(
        fmt::format("unknown edge kind '{}'; use one of {}", *kind_text, kind_list(kEdgeKinds))
      );
    }
    if (src->id == dst->id)
    {
      return std::unexpected(std::string {"an edge needs two different nodes"});
    }
    const auto weight = integer_arg(call.args(), "weight", 1);
    const auto evidence = text_arg(call.args(), "evidence");
    if (!weight || !evidence)
    {
      return std::unexpected(!weight ? weight.error() : evidence.error());
    }
    const bool added
      = store.upsert_edge(src->id, dst->id, *kind, std::max<std::int64_t>(*weight, 1), *evidence);
    return fmt::format(
      "{} {} -{}-> {}",
      added ? "added" : "updated",
      src->qualified_name,
      kEdgeKinds.at(static_cast<std::size_t>(*kind)),
      dst->qualified_name
    );
  }

  /// `chart_finish`: check and stamp the chart, and print the viewer command.
  ///
  /// \param call Call inputs.
  /// \return Summary and viewer command, or the problems found.
  Result
  chart_finish(Call &call)
  {
    Store &store = call.store();
    const std::vector<std::string> problems = store.check();
    if (!problems.empty())
    {
      return std::unexpected(fmt::format("the chart has problems:\n{}", fmt::join(problems, "\n")));
    }
    const std::string stamp = fmt::format("comprendo-cli {}", COMPRENDO_VERSION);
    store.set_meta("annotator", stamp);
    store.set_meta(
      "annotated_at",
      std::format(
        "{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())
      )
    );
    if (store.meta("generator").empty())
    {
      store.set_meta("generator", stamp);
    }
    if (store.meta("project").empty())
    {
      store.set_meta("project", call.root().lexically_normal().filename().string());
    }
    std::int64_t symbols = 0;
    std::int64_t documented = 0;
    std::int64_t annotated = 0;
    for (const FileProgress &file : store.files())
    {
      symbols += file.symbols;
      documented += file.documented;
      annotated += file.annotated;
    }
    return fmt::format(
      "chart: {}\n{} of {} symbols annotated by the model, {} documented in code, {} clusters\n"
      "open it with: comprendo {}",
      call.chart().string(),
      annotated,
      symbols,
      documented,
      store.clusters().size(),
      call.chart().string()
    );
  }

  /// A tool's description and handler.
  struct ToolSpec
  {
    /// Name, description, and schema.
    Comprendo::ToolInfo info;
    /// Handler.
    Handler handler;
  };

  /// Describe a string argument.
  ///
  /// \param description Meaning.
  /// \return JSON Schema.
  Json
  string_arg(std::string_view description)
  {
    return Json::Object {{"type", "string"}, {"description", description}};
  }

  /// Describe an integer argument.
  ///
  /// \param description Meaning.
  /// \return JSON Schema.
  Json
  integer_schema(std::string_view description)
  {
    return Json::Object {{"type", "integer"}, {"description", description}};
  }

  /// Build an argument object schema.
  ///
  /// \param properties Arguments by name.
  /// \return JSON Schema; no argument is required, the handlers check them.
  Json
  object_schema(Json::Object properties)
  {
    return Json::Object {
      {"type", "object"},
      {"properties", std::move(properties)},
      {"additionalProperties", false},
    };
  }

  /// The tool table, in listing order.
  ///
  /// \return Tools with their handlers.
  const std::vector<ToolSpec> &
  tool_specs()
  {
    static const std::vector<ToolSpec> specs = []
    {
      const std::string node_kinds = kind_list(kNodeKinds);
      const std::string edge_kinds = kind_list(kEdgeKinds);
      return std::vector<ToolSpec> {
        {
          .info = {
            .name = "chart_overview",
            .description
            = "Summarize the Comprendo chart: node and edge counts by kind, clusters, and per "
              "file the number of symbols and how many already have a summary. Start here.",
            .input_schema = object_schema({}),
          },
          .handler = chart_overview,
        },
        {
          .info = {
            .name = "chart_find_node",
            .description
            = "Find chart nodes and their ids. Matches the exact qualified name, the exact name, "
              "or a substring of the qualified name; empty query lists everything in `file`. Use "
              "the printed 0x… ids in the other chart tools.",
            .input_schema = object_schema({
              {"query", string_arg("Qualified name, name, or part of a name; empty for all")},
              {"file",
               string_arg(
                 "Repository-relative file or directory to search in; empty for all files"
               )},
              {"kind",
               string_arg(fmt::format("Restrict to one kind ({}); empty for all", node_kinds))},
              {"limit",
               integer_schema(
                 fmt::format("Maximum results (default 50, at most {})", kMaxFindResults)
               )},
            }),
          },
          .handler = chart_find_node,
        },
        {
          .info = {
            .name = "chart_add_node",
            .description
            = "Add a symbol the chart is missing (it is placed under `parent`, or under its "
              "file). Prefer annotating extracted nodes; use this for things the extractor "
              "cannot see.",
            .input_schema = object_schema({
              {"kind", string_arg(fmt::format("One of {}", node_kinds))},
              {"qualified_name", string_arg("Fully qualified name, e.g. geo::Circle::area")},
              {"file", string_arg("Repository-relative file declaring it")},
              {"line_start", integer_schema("First line, 1-based (0 if unknown)")},
              {"line_end", integer_schema("Last line, 1-based (0 if unknown)")},
              {"signature",
               string_arg(
                 "Signature distinguishing overloads, e.g. (double) const; may be empty"
               )},
              {"summary", string_arg("One-line explanation")},
              {"detail", string_arg("Longer explanation; may be empty")},
              {"parent", string_arg("Id of the enclosing node; empty for the file")},
            }),
          },
          .handler = chart_add_node,
        },
        {
          .info = {
            .name = "chart_annotate_node",
            .description
            = "Explain a node: a one-line summary (what it is for) and, for central pieces, a "
              "longer detail (how it works, invariants, pitfalls). Empty fields keep the current "
              "text.",
            .input_schema = object_schema({
              {"id", string_arg("Node id from chart_find_node")},
              {"summary", string_arg("One-line explanation; empty to keep")},
              {"detail", string_arg("Longer explanation in Markdown; empty to keep")},
            }),
          },
          .handler = chart_annotate_node,
        },
        {
          .info = {
            .name = "chart_annotate_cluster",
            .description
            = "Create or update a cluster (a subsystem or feature spanning files) and assign "
              "nodes to it. Clusters are identified by label; nodes belong to at most one "
              "cluster.",
            .input_schema = object_schema({
              {"label", string_arg("Short cluster name, e.g. Layout engine")},
              {"summary", string_arg("One-line explanation of the cluster")},
              {"color", string_arg("#RRGGBB, or empty to derive one from the label")},
              {"parent_label",
               string_arg("Label of an existing enclosing cluster; empty for none")},
              {"members",
               Json::Object {
                 {"type", "array"},
                 {"items", Json::Object {{"type", "string"}}},
                 {"description", "Node ids to assign"},
               }},
            }),
          },
          .handler = chart_annotate_cluster,
        },
        {
          .info = {
            .name = "chart_add_edge",
            .description = "Record a relationship between two nodes. Existing edges of the same "
                           "kind are updated.",
            .input_schema = object_schema({
              {"src", string_arg("Source node id")},
              {"dst", string_arg("Destination node id")},
              {"kind", string_arg(fmt::format("One of {}", edge_kinds))},
              {"weight", integer_schema("Strength, e.g. number of uses (default 1)")},
              {"evidence", string_arg("Where it shows, e.g. src/a.cpp:42; may be empty")},
            }),
          },
          .handler = chart_add_edge,
        },
        {
          .info = {
            .name = "chart_finish",
            .description
            = "Finish charting: stamp the chart, check its integrity, and return the command that "
              "opens it in the Comprendo viewer. Call once at the end.",
            .input_schema = object_schema({}),
          },
          .handler = chart_finish,
        },
      };
    }();
    return specs;
  }

  /// Open a chart for writing, creating it (and its directory) when absent.
  ///
  /// Refuses files that are not charts and charts with a newer schema, then
  /// enables WAL and foreign keys and applies the idempotent schema.
  ///
  /// \param path Chart file.
  /// \return The connection, or an error.
  std::expected<Database, std::string>
  open_chart(const std::filesystem::path &path)
  {
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    auto db = Database::open(path, Comprendo::Sqlite::OpenMode::ReadWriteCreate);
    if (!db)
    {
      return db;
    }
    const auto busy = db->exec(fmt::format("PRAGMA busy_timeout = {}", kBusyTimeoutMs));
    const auto application_id = db->query_int("PRAGMA application_id");
    const auto version = db->query_int("PRAGMA user_version");
    const auto objects = db->query_int("SELECT count(*) FROM sqlite_master");
    if (!busy || !application_id || !version || !objects)
    {
      return std::unexpected(
        !busy               ? busy.error()
          : !application_id ? application_id.error()
          : !version        ? version.error()
                            : objects.error()
      );
    }
    if (*application_id != Comprendo::kChartApplicationId && (*application_id != 0 || *objects > 0))
    {
      return std::unexpected(fmt::format("{} is not a Comprendo chart", path.string()));
    }
    if (std::cmp_greater(*version, Comprendo::kChartSchemaVersion))
    {
      return std::unexpected(
        fmt::format(
          "{} has chart schema version {}; comprendo-cli supports 1..{}",
          path.string(),
          *version,
          Comprendo::kChartSchemaVersion
        )
      );
    }
    const auto ready = db->exec("PRAGMA journal_mode = WAL; PRAGMA foreign_keys = ON;")
                         .and_then([&] { return db->exec(Comprendo::kChartSchemaSql); });
    if (!ready)
    {
      return std::unexpected(ready.error());
    }
    return db;
  }
} // namespace

namespace Comprendo
{
  ChartTools::ChartTools(const std::filesystem::path &root) :
    m_root(std::filesystem::absolute(root).lexically_normal()), m_chart(m_root / kChartPath)
  {
    for (const ToolSpec &spec : tool_specs())
    {
      m_tools.push_back(spec.info);
    }
  }

  std::expected<std::string, std::string>
  ChartTools::call(std::string_view name, const Json &args) const
  {
    const auto spec = std::ranges::find(
      tool_specs(), name, [](const ToolSpec &s) -> const std::string & { return s.info.name; }
    );
    if (spec == tool_specs().end())
    {
      return std::unexpected(fmt::format("unknown tool {}", name));
    }
    const Json empty = Json::Object {};
    const Json &arguments = args.as_object() != nullptr ? args : empty;
    auto db = open_chart(m_chart);
    if (!db)
    {
      return std::unexpected(db.error());
    }
    auto transaction = Sqlite::Transaction::begin(*db);
    if (!transaction)
    {
      return std::unexpected(transaction.error());
    }
    Store store {*db};
    Call call {store, arguments, m_root, m_chart};
    Result result = spec->handler(call);
    if (store.error())
    {
      return std::unexpected(*store.error());
    }
    if (!result)
    {
      return result;
    }
    if (auto committed = transaction->commit(); !committed)
    {
      return std::unexpected(committed.error());
    }
    return result;
  }

  std::uint64_t
  make_cluster_id(std::string_view label)
  {
    const std::uint64_t id = fnv1a64(fmt::format("cluster\x1f{}", label));
    return id == 0 ? 1 : id;
  }

  std::string
  format_id(std::uint64_t id)
  {
    return fmt::format("0x{:016x}", id);
  }

  std::optional<std::uint64_t>
  parse_id(std::string_view text)
  {
    constexpr std::size_t kMaxHexDigits = 16;
    constexpr std::size_t kMaxDecimalDigits = 20;
    constexpr int kHex = 16;
    constexpr int kDecimal = 10;
    std::string_view digits = trim(text);
    int base = kDecimal;
    std::size_t max_digits = kMaxDecimalDigits;
    if (digits.starts_with("0x") || digits.starts_with("0X"))
    {
      digits.remove_prefix(2);
      base = kHex;
      max_digits = kMaxHexDigits;
    }
    const auto valid_digit = [base](char c)
    {
      return base == kHex ? std::isxdigit(static_cast<unsigned char>(c)) != 0
                          : (c >= '0' && c <= '9');
    };
    if (digits.empty() || digits.size() > max_digits || !std::ranges::all_of(digits, valid_digit))
    {
      return std::nullopt;
    }
    std::uint64_t id = 0;
    const auto [end, error] = std::from_chars(digits.begin(), digits.end(), id, base);
    if (error != std::errc {} || end != digits.end())
    {
      return std::nullopt;
    }
    return id;
  }
} // namespace Comprendo
