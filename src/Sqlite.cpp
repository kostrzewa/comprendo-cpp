/// \file
/// \brief Thin RAII wrapper over the SQLite C API used by the chart store.

#include "Sqlite.hpp"

#include <sqlite3.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "Logger/Logger.hpp"

namespace
{
  /// Roll back the connection's open transaction, if SQLite has not already done so.
  ///
  /// \param db Connection to roll back.
  void
  rollback_if_open(Comprendo::Sqlite::Database &db)
  {
    if (sqlite3_get_autocommit(db.handle()) != 0)
    {
      return;
    }
    if (auto rolled_back = db.exec("ROLLBACK"); !rolled_back)
    {
      Comprendo::Logger::warn("SQLite rollback failed: {}", rolled_back.error());
    }
  }
} // namespace

namespace Comprendo::Sqlite
{
  void
  Database::Closer::operator()(sqlite3 *db) const noexcept
  {
    sqlite3_close_v2(db);
  }

  std::expected<Database, std::string>
  Database::open(const std::filesystem::path &path, OpenMode mode)
  {
    const int flags = mode == OpenMode::ReadOnly ? SQLITE_OPEN_READONLY
                                                 : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    sqlite3 *raw = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &raw, flags | SQLITE_OPEN_EXRESCODE, nullptr);
    Database db {raw};
    if (rc != SQLITE_OK)
    {
      const char *message = raw == nullptr ? sqlite3_errstr(rc) : sqlite3_errmsg(raw);
      return std::unexpected(fmt::format("cannot open {}: {}", path.string(), message));
    }
    return db;
  }

  std::expected<void, std::string>
  Database::exec(std::string_view sql)
  {
    const std::string text {sql};
    if (sqlite3_exec(m_db.get(), text.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
    {
      return std::unexpected(error_message());
    }
    return {};
  }

  std::expected<std::int64_t, std::string>
  Database::query_int(std::string_view sql)
  {
    auto stmt = Statement::prepare(*this, sql);
    if (!stmt)
    {
      return std::unexpected(stmt.error());
    }
    const auto row = stmt->step();
    if (!row)
    {
      return std::unexpected(row.error());
    }
    if (!*row)
    {
      return std::unexpected(fmt::format("query returned no row: {}", sql));
    }
    return stmt->column_int(0);
  }

  std::string
  Database::error_message() const
  {
    return sqlite3_errmsg(m_db.get());
  }

  void
  Statement::Finalizer::operator()(sqlite3_stmt *stmt) const noexcept
  {
    sqlite3_finalize(stmt);
  }

  std::expected<Statement, std::string>
  Statement::prepare(Database &db, std::string_view sql)
  {
    if (sql.size() > static_cast<std::size_t>(INT_MAX))
    {
      return std::unexpected("SQL statement is too long");
    }
    sqlite3_stmt *raw = nullptr;
    const int length = static_cast<int>(sql.size());
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage): the length is passed explicitly.
    if (sqlite3_prepare_v2(db.handle(), sql.data(), length, &raw, nullptr) != SQLITE_OK)
    {
      return std::unexpected(db.error_message());
    }
    return Statement {db, raw};
  }

  void
  Statement::record_bind(int rc)
  {
    if (rc != SQLITE_OK && m_bind_error.empty())
    {
      m_bind_error = m_db->error_message();
    }
  }

  void
  Statement::bind(int index, std::int64_t value)
  {
    record_bind(sqlite3_bind_int64(m_stmt.get(), index, value));
  }

  void
  Statement::bind(int index, double value)
  {
    record_bind(sqlite3_bind_double(m_stmt.get(), index, value));
  }

  void
  Statement::bind(int index, std::string_view value)
  {
    record_bind(sqlite3_bind_text64(
      m_stmt.get(), index, value.data(), value.size(), SQLITE_TRANSIENT, SQLITE_UTF8
    ));
  }

  void
  Statement::bind_null(int index)
  {
    record_bind(sqlite3_bind_null(m_stmt.get(), index));
  }

  std::expected<bool, std::string>
  Statement::step()
  {
    if (!m_bind_error.empty())
    {
      return std::unexpected(m_bind_error);
    }
    switch (sqlite3_step(m_stmt.get()))
    {
    case SQLITE_ROW: return true;
    case SQLITE_DONE: return false;
    default: return std::unexpected(m_db->error_message());
    }
  }

  void
  Statement::reset()
  {
    sqlite3_reset(m_stmt.get());
    sqlite3_clear_bindings(m_stmt.get());
    m_bind_error.clear();
  }

  std::int64_t
  Statement::column_int(int column) const
  {
    return sqlite3_column_int64(m_stmt.get(), column);
  }

  double
  Statement::column_double(int column) const
  {
    return sqlite3_column_double(m_stmt.get(), column);
  }

  std::string
  Statement::column_text(int column) const
  {
    const auto *text = sqlite3_column_text(m_stmt.get(), column);
    if (text == nullptr)
    {
      return {};
    }
    const int bytes = sqlite3_column_bytes(m_stmt.get(), column);
    // SQLite returns UTF-8 text as unsigned char.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const char *>(text), static_cast<std::size_t>(bytes)};
  }

  std::expected<Transaction, std::string>
  Transaction::begin(Database &db)
  {
    if (auto started = db.exec("BEGIN IMMEDIATE"); !started)
    {
      return std::unexpected(started.error());
    }
    return Transaction {db};
  }

  std::expected<void, std::string>
  Transaction::commit()
  {
    Database *db = std::exchange(m_db, nullptr);
    if (db == nullptr)
    {
      return std::unexpected("transaction is no longer open");
    }
    if (auto committed = db->exec("COMMIT"); !committed)
    {
      rollback_if_open(*db);
      return committed;
    }
    return {};
  }

  Transaction::~Transaction()
  {
    if (m_db != nullptr)
    {
      rollback_if_open(*m_db);
    }
  }
} // namespace Comprendo::Sqlite
