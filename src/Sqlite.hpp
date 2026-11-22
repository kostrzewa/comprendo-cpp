#pragma once

/// \file
/// \brief Thin RAII wrapper over the SQLite C API used by the chart store.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

struct sqlite3;
struct sqlite3_stmt;

namespace Comprendo::Sqlite
{
  /// How a database file is opened.
  enum class OpenMode : std::uint8_t
  {
    /// Open an existing file without write access.
    ReadOnly,
    /// Open for writing, creating the file when it does not exist.
    ReadWriteCreate,
  };

  /// An open SQLite connection.
  class Database
  {
  public:
    /// Open a database file.
    ///
    /// \param path Database file path.
    /// \param mode Read-only or read-write-create.
    /// \return The connection, or an error message naming \p path.
    [[nodiscard]]
    static std::expected<Database, std::string>
    open(const std::filesystem::path &path, OpenMode mode);

    /// Execute one or more SQL statements that return no rows of interest.
    ///
    /// \param sql SQL text, possibly containing several statements.
    /// \return Nothing, or the SQLite error message.
    [[nodiscard]]
    std::expected<void, std::string>
    exec(std::string_view sql);

    /// Run a query whose first row's first column is an integer, such as a pragma.
    ///
    /// \param sql Single-statement SQL query.
    /// \return The integer, or an error message when the query fails or yields no row.
    [[nodiscard]]
    std::expected<std::int64_t, std::string>
    query_int(std::string_view sql);

    /// Access the raw connection handle.
    ///
    /// \return Non-owning SQLite handle, valid while this object lives.
    [[nodiscard]]
    sqlite3 *
    handle() const
    {
      return m_db.get();
    }

    /// Fetch the most recent error message of this connection.
    ///
    /// \return SQLite's description of the last failure.
    [[nodiscard]]
    std::string
    error_message() const;

  private:
    /// Deleter closing a connection with `sqlite3_close_v2`.
    struct Closer
    {
      /// Close \p db.
      ///
      /// \param db Connection to close; may be null.
      void
      operator()(sqlite3 *db) const noexcept;
    };

    /// Take ownership of an open handle.
    ///
    /// \param db Open SQLite handle.
    explicit Database(sqlite3 *db) : m_db(db) { }

    /// Owned connection handle.
    std::unique_ptr<sqlite3, Closer> m_db;
  };

  /// A prepared statement.
  ///
  /// Bind failures are recorded and reported by the next call to step(), so a
  /// sequence of binds needs a single error check.
  class Statement
  {
  public:
    /// Compile a single SQL statement.
    ///
    /// \param db Connection the statement runs on; must outlive the statement.
    /// \param sql SQL text of exactly one statement.
    /// \return The statement, or the SQLite error message.
    [[nodiscard]]
    static std::expected<Statement, std::string>
    prepare(Database &db, std::string_view sql);

    /// Bind a 64-bit integer to a 1-based parameter index.
    ///
    /// \param index Parameter index, starting at 1.
    /// \param value Value to bind.
    void
    bind(int index, std::int64_t value);

    /// Bind a floating-point value to a 1-based parameter index.
    ///
    /// \param index Parameter index, starting at 1.
    /// \param value Value to bind.
    void
    bind(int index, double value);

    /// Bind UTF-8 text to a 1-based parameter index; the text is copied.
    ///
    /// \param index Parameter index, starting at 1.
    /// \param value Value to bind.
    void
    bind(int index, std::string_view value);

    /// Bind SQL NULL to a 1-based parameter index.
    ///
    /// \param index Parameter index, starting at 1.
    void
    bind_null(int index);

    /// Advance the statement.
    ///
    /// \return True when a row is available, false when done, or the error message
    ///         of the failing step or of an earlier bind.
    [[nodiscard]]
    std::expected<bool, std::string>
    step();

    /// Reset the statement and clear its bindings so it can run again.
    void
    reset();

    /// Read an integer column of the current row.
    ///
    /// \param column 0-based column index.
    /// \return Column value; 0 for NULL.
    [[nodiscard]]
    std::int64_t
    column_int(int column) const;

    /// Read a floating-point column of the current row.
    ///
    /// \param column 0-based column index.
    /// \return Column value; 0.0 for NULL.
    [[nodiscard]]
    double
    column_double(int column) const;

    /// Read a text column of the current row.
    ///
    /// \param column 0-based column index.
    /// \return Column value as UTF-8; empty for NULL.
    [[nodiscard]]
    std::string
    column_text(int column) const;

  private:
    /// Deleter finalizing a statement.
    struct Finalizer
    {
      /// Finalize \p stmt.
      ///
      /// \param stmt Statement to finalize; may be null.
      void
      operator()(sqlite3_stmt *stmt) const noexcept;
    };

    /// Take ownership of a prepared statement.
    ///
    /// \param db Connection the statement belongs to.
    /// \param stmt Prepared statement handle.
    Statement(Database &db, sqlite3_stmt *stmt) : m_db(&db), m_stmt(stmt) { }

    /// Record the first failing bind result code.
    ///
    /// \param rc Result code returned by a `sqlite3_bind_*` call.
    void
    record_bind(int rc);

    /// Connection used for error messages.
    Database *m_db;
    /// Owned statement handle.
    std::unique_ptr<sqlite3_stmt, Finalizer> m_stmt;
    /// First bind error since the last reset, empty when none.
    std::string m_bind_error;
  };

  /// A transaction that rolls back unless committed.
  class Transaction
  {
  public:
    /// Start an immediate transaction.
    ///
    /// \param db Connection to run the transaction on; must outlive the transaction.
    /// \return The transaction, or the SQLite error message.
    [[nodiscard]]
    static std::expected<Transaction, std::string>
    begin(Database &db);

    /// Commit the transaction.
    ///
    /// \return Nothing, or the SQLite error message (for example a deferred
    ///         foreign-key violation). The transaction is rolled back on failure.
    [[nodiscard]]
    std::expected<void, std::string>
    commit();

    /// Roll back when the transaction is still open.
    ~Transaction();

    /// Transfer an open transaction.
    ///
    /// \param other Transaction to take over; left inactive.
    Transaction(Transaction &&other) noexcept : m_db(std::exchange(other.m_db, nullptr)) { }

    Transaction(const Transaction &) = delete;
    Transaction &
    operator=(const Transaction &) = delete;
    Transaction &
    operator=(Transaction &&) = delete;

  private:
    /// Wrap an already started transaction.
    ///
    /// \param db Connection with an open transaction.
    explicit Transaction(Database &db) : m_db(&db) { }

    /// Connection with the open transaction, or null once finished.
    Database *m_db;
  };
} // namespace Comprendo::Sqlite
