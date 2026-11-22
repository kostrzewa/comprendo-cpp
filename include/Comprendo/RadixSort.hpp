#pragma once

/// \file
/// \brief LSD radix sort for 64-bit keys, used to order bodies along a Morton curve.

#include <cstdint>
#include <span>

namespace Comprendo
{
  /// Sort 64-bit keys in ascending order.
  ///
  /// Least-significant-digit radix sort with 11-bit digits (six passes). All
  /// digit histograms are built in one read, and passes in which every key has
  /// the same digit are skipped. Fewer than 256 keys are sorted with
  /// std::ranges::sort, which is faster at that size. The result is the unique
  /// ascending order, so it does not depend on the algorithm used.
  ///
  /// \param keys Keys to sort in place.
  void
  radix_sort(std::span<std::uint64_t> keys);
} // namespace Comprendo
