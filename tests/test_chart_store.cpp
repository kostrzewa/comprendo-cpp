/// \file
/// \brief Tests for the SQLite chart store: round-trips, identity checks, and atomic writes.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <sqlite3.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

#include "ChartSchema.hpp"
#include "Comprendo/Chart.hpp"
#include "Comprendo/ChartStore.hpp"

namespace
{
  using Catch::Matchers::ContainsSubstring;
  using Comprendo::Chart;
  using Comprendo::EdgeKind;
  using Comprendo::NodeKind;
  using Comprendo::Provenance;

  /// A scratch directory removed when the test ends.
  class TempDir
  {
  public:
    /// Create an empty directory under the system temp directory.
    ///
    /// \param name Directory name unique to the test.
    explicit TempDir(std::string_view name) :
      m_path(std::filesystem::temp_directory_path() / "comprendo-tests" / name)
    {
      std::filesystem::remove_all(m_path);
      std::filesystem::create_directories(m_path);
    }

    /// Remove the directory and its contents.
    ~TempDir()
    {
      std::error_code ignored;
      std::filesystem::remove_all(m_path, ignored);
    }

    TempDir(const TempDir &) = delete;
    TempDir &
    operator=(const TempDir &) = delete;
    TempDir(TempDir &&) = delete;
    TempDir &
    operator=(TempDir &&) = delete;

    /// Build a path inside the directory.
    ///
    /// \param name File name.
    /// \return Path of \p name inside the directory.
    [[nodiscard]]
    std::filesystem::path
    operator/(std::string_view name) const
    {
      return m_path / name;
    }

  private:
    /// Directory path.
    std::filesystem::path m_path;
  };

  /// Run SQL on a database file through the raw SQLite API.
  ///
  /// \param path Database file, created when missing.
  /// \param sql SQL statements to execute.
  /// \return SQLite result code of the execution.
  int
  exec_raw(const std::filesystem::path &path, const std::string &sql)
  {
    sqlite3 *db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc == SQLITE_OK)
    {
      rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    }
    sqlite3_close(db);
    return rc;
  }

  /// Read a single integer through the raw SQLite API.
  ///
  /// \param path Database file.
  /// \param sql Query returning one integer.
  /// \return The integer, or -1 when the query fails.
  std::int64_t
  query_raw(const std::filesystem::path &path, const std::string &sql)
  {
    sqlite3 *db = nullptr;
    std::int64_t value = -1;
    sqlite3_stmt *stmt = nullptr;
    if (
      sqlite3_open(path.c_str(), &db) == SQLITE_OK
      && sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK
      && sqlite3_step(stmt) == SQLITE_ROW
    )
    {
      value = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return value;
  }

  /// Build a chart exercising every column type and edge case the store must preserve.
  ///
  /// Ids with the high bit set, UTF-8 text, all kinds, nested clusters listed
  /// child-first, and a full layout.
  ///
  /// \return Valid chart in non-canonical order.
  Chart
  populated_chart()
  {
    constexpr std::uint64_t kHighBitId = 0x8000000000000001ULL;
    constexpr std::uint64_t kMaxId = 0xFFFFFFFFFFFFFFFFULL;
    constexpr std::uint64_t kParent = 0xF00DULL;
    constexpr std::uint64_t kChild = 0x9000000000000000ULL;

    Chart chart;
    chart.project = "comprendo";
    chart.git_commit = "fff4db5-dirty";
    chart.generated_at = "2026-11-22T18:00:00Z";
    chart.generator = "comprendo-cli 0.1.0";
    chart.source_root = "/home/user/zażółć/項目";

    chart.clusters.push_back(
      {.id = kChild, .label = "child", .summary = "nested", .color = 0xFF8000, .parent_id = kParent}
    );
    chart.clusters.push_back({.id = kParent, .label = "parent", .color = 0x123456});

    for (std::uint8_t kind = 0; kind <= std::to_underlying(NodeKind::TypeAlias); ++kind)
    {
      const auto node_kind = static_cast<NodeKind>(kind);
      const std::string name = "symbol" + std::to_string(kind);
      chart.nodes.push_back({
        .id = Comprendo::make_node_id(node_kind, "ns::" + name, "src/file.cpp", ""),
        .kind = node_kind,
        .name = name,
        .qualified_name = "ns::" + name,
        .file = "src/file.cpp",
        .line_start = kind + 1U,
        .line_end = kind + 10U,
        .signature = "void (int)",
        .summary = "Fait quelque chose — 🦀",
        .detail = "Line one\nLine two\twith tab and a NUL-free 'quote'",
        .cluster_id = kind % 2 == 0 ? kChild : 0,
        .loc = 100U + kind,
        .complexity = kind,
        .provenance = static_cast<Provenance>(kind % 3),
      });
    }
    chart.nodes.push_back({.id = kHighBitId, .kind = NodeKind::Class, .name = "High"});
    chart.nodes.push_back({.id = kMaxId, .kind = NodeKind::Struct, .name = "Max"});

    for (std::uint8_t kind = 0; kind <= std::to_underlying(EdgeKind::DataFlow); ++kind)
    {
      chart.edges.push_back({
        .src = kMaxId,
        .dst = kHighBitId,
        .kind = static_cast<EdgeKind>(kind),
        .weight = 0.25F * kind,
        .evidence = "src/file.cpp:" + std::to_string(kind),
        .provenance = static_cast<Provenance>(kind % 3),
      });
    }
    chart.edges.push_back({.src = chart.nodes[0].id, .dst = kMaxId, .kind = EdgeKind::Contains});

    for (std::size_t index = 0; index < chart.nodes.size(); ++index)
    {
      chart.layout.push_back(
        {.x = static_cast<float>(index) * 1.5F, .y = -static_cast<float>(index) / 3.0F}
      );
    }
    return chart;
  }

  /// Return a canonically ordered copy of a chart, as read_chart produces it.
  ///
  /// \param chart Chart to copy.
  /// \return Canonical copy.
  Chart
  canonical(Chart chart)
  {
    Comprendo::canonicalize(chart);
    return chart;
  }
} // namespace

/// A populated chart survives a write/read cycle unchanged.
TEST_CASE("populated chart round-trips", "[store]")
{
  const TempDir dir {"round-trip"};
  const Chart chart = populated_chart();
  REQUIRE(Comprendo::validate(chart).ok());

  const auto path = dir / "nested/dir/chart.db";
  const auto written = Comprendo::write_chart(path, chart);
  REQUIRE(written.has_value());

  const auto read = Comprendo::read_chart(path);
  REQUIRE(read.has_value());
  CHECK(*read == canonical(chart));
}

/// An empty chart round-trips and keeps its header.
TEST_CASE("empty chart round-trips", "[store]")
{
  const TempDir dir {"empty"};
  Chart chart;
  chart.project = "empty";

  REQUIRE(Comprendo::write_chart(dir / "chart.db", chart).has_value());
  const auto read = Comprendo::read_chart(dir / "chart.db");
  REQUIRE(read.has_value());
  CHECK(*read == chart);
}

/// A chart without a cached layout reads back without one.
TEST_CASE("chart without layout round-trips", "[store]")
{
  const TempDir dir {"no-layout"};
  Chart chart = populated_chart();
  chart.layout.clear();

  REQUIRE(Comprendo::write_chart(dir / "chart.db", chart).has_value());
  const auto read = Comprendo::read_chart(dir / "chart.db");
  REQUIRE(read.has_value());
  CHECK(read->layout.empty());
  CHECK(*read == canonical(chart));
}

/// A layout missing a node's position is a stale cache and reads as empty.
TEST_CASE("partial layout reads as empty", "[store]")
{
  const TempDir dir {"partial-layout"};
  const auto path = dir / "chart.db";
  REQUIRE(Comprendo::write_chart(path, populated_chart()).has_value());
  REQUIRE(
    exec_raw(path, "DELETE FROM layout WHERE rowid = (SELECT MIN(rowid) FROM layout)") == SQLITE_OK
  );

  const auto read = Comprendo::read_chart(path);
  REQUIRE(read.has_value());
  CHECK(read->layout.empty());
  CHECK_FALSE(read->nodes.empty());
}

/// Writing again replaces the previous chart completely.
TEST_CASE("writing overwrites an existing chart", "[store]")
{
  const TempDir dir {"overwrite"};
  const auto path = dir / "chart.db";
  REQUIRE(Comprendo::write_chart(path, populated_chart()).has_value());

  Chart second;
  second.project = "second";
  second.nodes.push_back({.id = 5, .kind = NodeKind::File, .name = "only.cpp"});
  REQUIRE(Comprendo::write_chart(path, second).has_value());

  const auto read = Comprendo::read_chart(path);
  REQUIRE(read.has_value());
  CHECK(*read == second);
}

/// An invalid chart is refused and the existing file is left as it was.
TEST_CASE("failed write leaves the existing chart untouched", "[store]")
{
  const TempDir dir {"atomic"};
  const auto path = dir / "chart.db";
  const Chart original = populated_chart();
  REQUIRE(Comprendo::write_chart(path, original).has_value());

  Chart dangling;
  dangling.nodes.push_back({.id = 1});
  dangling.edges.push_back({.src = 1, .dst = 2, .kind = EdgeKind::Calls});
  const auto written = Comprendo::write_chart(path, dangling);
  REQUIRE_FALSE(written.has_value());
  CHECK_THAT(written.error(), ContainsSubstring("edge destination 2 does not exist"));

  CHECK_FALSE(std::filesystem::exists(dir / "chart.db.tmp"));
  const auto read = Comprendo::read_chart(path);
  REQUIRE(read.has_value());
  CHECK(*read == canonical(original));
}

/// Written files carry the Comprendo identity pragmas and the supported schema version.
TEST_CASE("written chart carries identity pragmas", "[store]")
{
  const TempDir dir {"identity"};
  const auto path = dir / "chart.db";
  REQUIRE(Comprendo::write_chart(path, Chart {}).has_value());

  CHECK(query_raw(path, "PRAGMA application_id") == Comprendo::kChartApplicationId);
  CHECK(query_raw(path, "PRAGMA user_version") == Comprendo::kChartSchemaVersion);
  CHECK_FALSE(std::filesystem::exists(dir / "chart.db-wal"));

  constexpr std::size_t kHeaderBytes = 20;
  constexpr std::size_t kWriteVersionOffset = 18;
  constexpr char kWalVersion = 2;
  std::array<char, kHeaderBytes> header {};
  std::ifstream {path, std::ios::binary}.read(header.data(), header.size());
  CHECK(header[kWriteVersionOffset] == kWalVersion);
  CHECK(header[kWriteVersionOffset + 1] == kWalVersion);
}

/// The shared DDL can be applied repeatedly, as both the plugin and the CLI do.
TEST_CASE("schema DDL is idempotent", "[store]")
{
  const TempDir dir {"idempotent"};
  const auto path = dir / "chart.db";
  const std::string schema {Comprendo::kChartSchemaSql};
  REQUIRE(exec_raw(path, schema) == SQLITE_OK);
  REQUIRE(exec_raw(path, schema) == SQLITE_OK);
  CHECK(Comprendo::read_chart(path).has_value());
}

/// The SQL constraints reject rows the in-memory validation would reject.
TEST_CASE("schema constraints enforce invariants", "[store]")
{
  const TempDir dir {"constraints"};
  const auto path = dir / "chart.db";
  REQUIRE(Comprendo::write_chart(path, Chart {}).has_value());
  const std::string node = "INSERT INTO nodes (id, kind, name, qualified_name, file, provenance) "
                           "VALUES ";

  CHECK(exec_raw(path, node + "(1, 14, 'n', 'n', 'f', 0)") != SQLITE_OK);
  CHECK(exec_raw(path, node + "(1, 0, 'n', 'n', 'f', 3)") != SQLITE_OK);
  CHECK(exec_raw(path, "INSERT INTO clusters (id, label) VALUES (0, 'zero')") != SQLITE_OK);
  CHECK(
    exec_raw(path, "INSERT INTO edges (src, dst, kind, provenance) VALUES (1, 2, 3, 1)")
    != SQLITE_OK
  );
  REQUIRE(exec_raw(path, node + "(1, 0, 'n', 'n', 'f', 0)") == SQLITE_OK);
  CHECK(exec_raw(path, "UPDATE nodes SET cluster_id = 5 WHERE id = 1") != SQLITE_OK);
}

/// Reading a file that does not exist reports it by name.
TEST_CASE("reading a missing chart fails", "[store]")
{
  const TempDir dir {"missing"};
  const auto read = Comprendo::read_chart(dir / "absent.db");
  REQUIRE_FALSE(read.has_value());
  CHECK_THAT(read.error(), ContainsSubstring("does not exist"));
  CHECK_FALSE(std::filesystem::exists(dir / "absent.db"));
}

/// A file that is not a SQLite database is rejected.
TEST_CASE("reading a non-database file fails", "[store]")
{
  const TempDir dir {"garbage"};
  const auto path = dir / "chart.db";
  std::ofstream {path} << "this is not a database, just some text that is long enough";

  const auto read = Comprendo::read_chart(path);
  REQUIRE_FALSE(read.has_value());
  CHECK_THAT(read.error(), ContainsSubstring("cannot read"));
}

/// A SQLite database from another application is rejected.
TEST_CASE("reading a foreign database fails", "[store]")
{
  const TempDir dir {"foreign"};
  const auto path = dir / "other.db";
  REQUIRE(exec_raw(path, "CREATE TABLE t (x INTEGER)") == SQLITE_OK);

  const auto read = Comprendo::read_chart(path);
  REQUIRE_FALSE(read.has_value());
  CHECK_THAT(read.error(), ContainsSubstring("not a Comprendo chart"));
}

/// A chart from a newer schema version is refused with the supported range.
TEST_CASE("reading a newer schema version fails", "[store]")
{
  const TempDir dir {"newer"};
  const auto path = dir / "chart.db";
  REQUIRE(Comprendo::write_chart(path, Chart {}).has_value());
  REQUIRE(exec_raw(path, "PRAGMA user_version = 2") == SQLITE_OK);

  const auto read = Comprendo::read_chart(path);
  REQUIRE_FALSE(read.has_value());
  CHECK_THAT(
    read.error(), ContainsSubstring("unsupported chart schema version 2 (supported 1..1)")
  );
}

/// Reading never modifies the chart file.
TEST_CASE("reading leaves the file unchanged", "[store]")
{
  const TempDir dir {"read-only"};
  const auto path = dir / "chart.db";
  REQUIRE(Comprendo::write_chart(path, populated_chart()).has_value());
  const auto size = std::filesystem::file_size(path);
  const auto modified = std::filesystem::last_write_time(path);

  REQUIRE(Comprendo::read_chart(path).has_value());
  CHECK(std::filesystem::file_size(path) == size);
  CHECK(std::filesystem::last_write_time(path) == modified);
}
