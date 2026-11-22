#pragma once

/// \file
/// \brief Helpers that let tests inspect results without dereferencing failed values.

#include <expected>
#include <string>
#include <vector>

namespace Comprendo::Testing
{
  /// Error text of a result, safe to call whether or not it holds a value.
  ///
  /// \param result Result to inspect.
  /// \return The error message, or an empty string when \p result holds a value.
  template <typename T>
  std::string
  error_of(const std::expected<T, std::string> &result)
  {
    return result.has_value() ? std::string {} : result.error();
  }

  /// Join error messages into one string for substring checks.
  ///
  /// \param errors Messages to join.
  /// \return The messages separated by newlines; empty when there are none.
  inline std::string
  joined(const std::vector<std::string> &errors)
  {
    std::string text;
    for (const std::string &error : errors)
    {
      text += text.empty() ? "" : "\n";
      text += error;
    }
    return text;
  }
} // namespace Comprendo::Testing
