/// \file
/// \brief LSD radix sort for 64-bit keys, used to order bodies along a Morton curve.

#include "Comprendo/RadixSort.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
  /// Bits per radix digit.
  constexpr unsigned kDigitBits = 11;
  /// Buckets per digit.
  constexpr std::size_t kBuckets = std::size_t {1} << kDigitBits;
  /// Passes needed to cover 64 bits.
  constexpr unsigned kPasses = (64 + kDigitBits - 1) / kDigitBits;
  /// Mask selecting one digit.
  constexpr std::uint64_t kDigitMask = kBuckets - 1;
  /// Key count below which a comparison sort is faster.
  constexpr std::size_t kRadixThreshold = 256;

  /// Extract one digit of a key.
  ///
  /// \param key Key.
  /// \param pass Digit index, 0 for the least significant.
  /// \return Digit value below kBuckets.
  std::size_t
  digit(std::uint64_t key, unsigned pass)
  {
    return static_cast<std::size_t>((key >> (pass * kDigitBits)) & kDigitMask);
  }
} // namespace

namespace Comprendo
{
  void
  radix_sort(std::span<std::uint64_t> keys)
  {
    const std::size_t count = keys.size();
    if (count < kRadixThreshold)
    {
      std::ranges::sort(keys);
      return;
    }

    std::vector<std::array<std::uint32_t, kBuckets>> histograms(kPasses);
    for (const std::uint64_t key : keys)
    {
      for (unsigned pass = 0; pass < kPasses; ++pass)
      {
        ++histograms[pass].at(digit(key, pass));
      }
    }

    std::vector<std::uint64_t> buffer(count);
    std::span<std::uint64_t> source = keys;
    std::span<std::uint64_t> target = buffer;
    for (unsigned pass = 0; pass < kPasses; ++pass)
    {
      std::array<std::uint32_t, kBuckets> &histogram = histograms[pass];
      if (histogram.at(digit(source.front(), pass)) == count)
      {
        continue;
      }
      std::uint32_t offset = 0;
      for (std::uint32_t &bucket : histogram)
      {
        const std::uint32_t size = bucket;
        bucket = offset;
        offset += size;
      }
      for (const std::uint64_t key : source)
      {
        target[histogram.at(digit(key, pass))++] = key;
      }
      std::swap(source, target);
    }
    if (source.data() != keys.data())
    {
      std::ranges::copy(source, keys.begin());
    }
  }
} // namespace Comprendo
