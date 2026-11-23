/// \file
/// \brief Pure navigation helpers for the viewer: list cursors, camera animations, and history.

#include "Comprendo/Navigation.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace Comprendo
{
  std::optional<std::size_t>
  move_list_cursor(std::optional<std::size_t> cursor, std::size_t rows, int delta)
  {
    if (rows == 0)
    {
      return std::nullopt;
    }
    if (!cursor)
    {
      return 0;
    }
    const auto last = static_cast<std::ptrdiff_t>(rows - 1);
    const std::ptrdiff_t moved = static_cast<std::ptrdiff_t>(*cursor) + delta;
    return static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(moved, 0, last));
  }
} // namespace Comprendo
