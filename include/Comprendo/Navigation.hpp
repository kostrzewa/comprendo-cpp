#pragma once

/// \file
/// \brief Pure navigation helpers for the viewer: list cursors, camera animations, and history.

#include <cstddef>
#include <optional>

namespace Comprendo
{
  /// Move a keyboard cursor through a list.
  ///
  /// Moving from no cursor lands on the first row; the cursor stays within the
  /// list and disappears when the list is empty.
  ///
  /// \param cursor Current row, if any.
  /// \param rows Number of rows.
  /// \param delta Rows to move (negative moves up).
  /// \return The new row, or nothing for an empty list.
  [[nodiscard]]
  std::optional<std::size_t>
  move_list_cursor(std::optional<std::size_t> cursor, std::size_t rows, int delta);
} // namespace Comprendo
