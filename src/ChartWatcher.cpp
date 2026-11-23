/// \file
/// \brief Detects changes to a chart file made by other writers.

#include "Comprendo/ChartWatcher.hpp"

#include <sys/stat.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

#include "Logger/Logger.hpp"
#include "Sqlite.hpp"

namespace
{
  /// Identity of a file: replacing the file changes it even when the path stays.
  struct FileId
  {
    /// Device number.
    std::uint64_t device = 0;
    /// Inode number.
    std::uint64_t inode = 0;

    /// Memberwise equality.
    bool
    operator==(const FileId &) const = default;
  };

  /// Read a file's identity.
  ///
  /// \param path File path.
  /// \return The identity, or nothing when the file does not exist.
  std::optional<FileId>
  file_id(const std::filesystem::path &path)
  {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0)
    {
      return std::nullopt;
    }
    return FileId {.device = static_cast<std::uint64_t>(info.st_dev), .inode = info.st_ino};
  }
} // namespace

namespace Comprendo
{
  /// Connection and last observed state.
  struct ChartWatcher::State
  {
    /// Watched file.
    std::filesystem::path path;
    /// Read-only connection to the current file, when it exists and opens.
    std::optional<Sqlite::Database> db;
    /// Identity of the file the connection belongs to.
    std::optional<FileId> id;
    /// Last `PRAGMA data_version` read on the connection.
    std::optional<std::int64_t> version;
    /// True once the first call recorded a baseline.
    bool started = false;
  };

  ChartWatcher::ChartWatcher(std::filesystem::path path) : m_state(std::make_unique<State>())
  {
    m_state->path = std::move(path);
  }

  ChartWatcher::~ChartWatcher() = default;

  ChartWatcher::ChartWatcher(ChartWatcher &&other) noexcept = default;

  ChartWatcher &
  ChartWatcher::operator=(ChartWatcher &&other) noexcept = default;

  bool
  ChartWatcher::changed()
  {
    State &state = *m_state;
    const std::optional<FileId> id = file_id(state.path);
    const bool replaced = id != state.id;
    if (replaced)
    {
      state.id = id;
      state.db.reset();
      if (id)
      {
        auto db = Sqlite::Database::open(state.path, Sqlite::OpenMode::ReadOnly);
        if (db)
        {
          state.db = std::move(*db);
        }
        else
        {
          Logger::debug("chart watcher cannot open {}: {}", state.path.string(), db.error());
        }
      }
    }
    std::optional<std::int64_t> version;
    if (state.db)
    {
      auto queried = state.db->query_int("PRAGMA data_version");
      version = queried ? std::optional {*queried} : std::nullopt;
    }
    const bool committed = !replaced && version != state.version;
    state.version = version;
    const bool first = !state.started;
    state.started = true;
    return !first && (replaced || committed);
  }
} // namespace Comprendo
