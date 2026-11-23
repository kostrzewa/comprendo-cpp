/// \file
/// \brief Tests for detecting chart changes made by other writers.

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <filesystem>
#include <string>

#include "Comprendo/Chart.hpp"
#include "Comprendo/ChartStore.hpp"
#include "Comprendo/ChartWatcher.hpp"
#include "TempDir.hpp"

namespace
{
  /// A chart with one file node.
  ///
  /// \param file Repository-relative path of the file.
  /// \return The chart.
  Comprendo::Chart
  one_file_chart(const std::string &file)
  {
    Comprendo::Chart chart;
    chart.nodes.push_back({
      .id = Comprendo::make_node_id(Comprendo::NodeKind::File, file, file, ""),
      .kind = Comprendo::NodeKind::File,
      .name = file,
      .qualified_name = file,
      .file = file,
      .provenance = Comprendo::Provenance::Extractor,
    });
    return chart;
  }

  /// Commit a statement through a separate connection, as the plugin does.
  ///
  /// \param path Chart file.
  /// \param sql Statement to run.
  /// \return True when the statement ran.
  bool
  commit(const std::filesystem::path &path, const char *sql)
  {
    sqlite3 *db = nullptr;
    const bool opened
      = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK;
    const bool ran = opened && sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close_v2(db);
    return ran;
  }
} // namespace

/// The viewer reloads when the plugin annotates in place or the CLI rewrites the
/// file, and must stay quiet otherwise so it does not relayout every frame.
/// Starts watching a missing chart, then creates it, polls without changes,
/// commits an update through another connection, rewrites it with write_chart,
/// and deletes it, checking changed() after each step.
TEST_CASE("chart watcher reports commits, replacements, and removal", "[watcher]")
{
  const Comprendo::Testing::TempDir dir {"chart-watcher"};
  const std::filesystem::path path = dir / "chart.sqlite";
  Comprendo::ChartWatcher watcher {path};
  CHECK_FALSE(watcher.changed());
  CHECK_FALSE(watcher.changed());

  CHECK(Comprendo::write_chart(path, one_file_chart("a.cpp")).has_value());
  CHECK(watcher.changed());
  CHECK_FALSE(watcher.changed());

  CHECK(commit(path, "UPDATE nodes SET summary = 'annotated'"));
  CHECK(watcher.changed());
  CHECK_FALSE(watcher.changed());

  CHECK(Comprendo::write_chart(path, one_file_chart("b.cpp")).has_value());
  CHECK(watcher.changed());
  CHECK_FALSE(watcher.changed());

  std::filesystem::remove(path);
  CHECK(watcher.changed());
  CHECK_FALSE(watcher.changed());
}

/// The first poll only records a baseline: an existing chart must not trigger a
/// reload right after the viewer loaded it. Writes a chart, starts a watcher on
/// it, and checks the first two polls report nothing.
TEST_CASE("chart watcher starts quiet on an existing chart", "[watcher]")
{
  const Comprendo::Testing::TempDir dir {"chart-watcher-existing"};
  const std::filesystem::path path = dir / "chart.sqlite";
  CHECK(Comprendo::write_chart(path, one_file_chart("a.cpp")).has_value());
  Comprendo::ChartWatcher watcher {path};
  CHECK_FALSE(watcher.changed());
  CHECK_FALSE(watcher.changed());
}
