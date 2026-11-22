#pragma once

/// \file
/// \brief Scratch directory for tests, removed when the test ends.

#include <filesystem>
#include <string_view>
#include <system_error>

namespace Comprendo::Testing
{
  /// A scratch directory under the system temp directory, removed on destruction.
  class TempDir
  {
  public:
    /// Create an empty directory, replacing leftovers from an earlier run.
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

    /// Access the directory path.
    ///
    /// \return Absolute directory path.
    [[nodiscard]]
    const std::filesystem::path &
    path() const
    {
      return m_path;
    }

    /// Build a path inside the directory.
    ///
    /// \param name Relative path.
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
} // namespace Comprendo::Testing
