#pragma once

/// \file
/// \brief Detects changes to a chart file made by other writers.

#include <filesystem>
#include <memory>

namespace Comprendo
{
  /// Watches a chart file for commits by other processes.
  ///
  /// Two signals are combined: SQLite's `PRAGMA data_version` on a long-lived
  /// read-only connection, which changes when another connection commits (the
  /// plugin annotating in place), and the file's identity (device and inode),
  /// which changes when the file is replaced (`comprendo-cli extract` deletes
  /// and rewrites it) or appears or disappears.
  class ChartWatcher
  {
  public:
    /// Start watching; the chart need not exist yet.
    ///
    /// \param path Chart file.
    explicit ChartWatcher(std::filesystem::path path);

    /// Close the watching connection.
    ~ChartWatcher();

    ChartWatcher(const ChartWatcher &) = delete;
    ChartWatcher &
    operator=(const ChartWatcher &) = delete;

    /// Take over another watcher.
    ///
    /// \param other Watcher to move from; unusable afterwards.
    ChartWatcher(ChartWatcher &&other) noexcept;

    /// Take over another watcher.
    ///
    /// \param other Watcher to move from; unusable afterwards.
    /// \return This watcher.
    ChartWatcher &
    operator=(ChartWatcher &&other) noexcept;

    /// Check for changes since the previous call.
    ///
    /// The first call records the current state and returns false.
    ///
    /// \return True when the chart was committed to, replaced, created, or removed.
    [[nodiscard]]
    bool
    changed();

  private:
    /// Connection and last observed state.
    struct State;

    /// Watch state; null only after a move.
    std::unique_ptr<State> m_state;
  };
} // namespace Comprendo
