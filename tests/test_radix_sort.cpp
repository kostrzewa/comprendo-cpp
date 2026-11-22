/// \file
/// \brief Tests for the 64-bit LSD radix sort.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "Comprendo/RadixSort.hpp"
#include "Comprendo/Random.hpp"

namespace
{
  /// Sort a copy with std::ranges::sort and with radix_sort and compare.
  ///
  /// \param keys Keys to sort.
  /// \return True when both orders are identical.
  bool
  sorts_like_std(std::vector<std::uint64_t> keys)
  {
    std::vector<std::uint64_t> expected = keys;
    std::ranges::sort(expected);
    Comprendo::radix_sort(keys);
    return keys == expected;
  }
} // namespace

/// QuadTree orders bodies by radix-sorting packed Morton keys, so the sort must
/// equal std::sort for every input it can see. Compares against std::ranges::sort
/// for random 64-bit keys at sizes around the 256-key comparison-sort threshold
/// and up to 50k, for keys with heavy duplication, and for the extreme values
/// 0 and UINT64_MAX, which exercise the most significant 11-bit digit.
TEST_CASE("radix sort matches std::sort", "[radix]")
{
  Comprendo::SplitMix64 random {5};
  for (const std::size_t size : {0U, 1U, 2U, 255U, 256U, 257U, 1000U, 50000U})
  {
    std::vector<std::uint64_t> keys(size);
    for (std::uint64_t &key : keys)
    {
      key = random.next();
    }
    INFO("size " << size);
    CHECK(sorts_like_std(keys));
  }

  std::vector<std::uint64_t> duplicated(5000);
  for (std::uint64_t &key : duplicated)
  {
    key = random.next() % 7;
  }
  CHECK(sorts_like_std(duplicated));

  std::vector<std::uint64_t> extremes(1000, 0);
  for (std::size_t i = 0; i < extremes.size(); i += 3)
  {
    extremes[i] = std::numeric_limits<std::uint64_t>::max() - i;
  }
  CHECK(sorts_like_std(extremes));
}

/// Packed keys share many high digits (the Morton code of nearby bodies, the
/// unused high bits of small indices), and passes over a constant digit are
/// skipped; the skip must not lose the ordering established by earlier passes.
/// Sorts 4096 keys whose upper 40 bits are constant, and keys whose only
/// variation is in the top digit, and compares with std::ranges::sort.
TEST_CASE("radix sort skips constant digits correctly", "[radix]")
{
  Comprendo::SplitMix64 random {9};
  std::vector<std::uint64_t> low_only(4096);
  for (std::uint64_t &key : low_only)
  {
    key = (0xabcdef1234ULL << 24U) | (random.next() & 0xffffffULL);
  }
  CHECK(sorts_like_std(low_only));

  std::vector<std::uint64_t> high_only(4096);
  for (std::uint64_t &key : high_only)
  {
    key = (random.next() & 0x1ffULL) << 55U;
  }
  CHECK(sorts_like_std(high_only));
}
