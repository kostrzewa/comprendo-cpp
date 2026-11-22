#pragma once

/// \file
/// \brief SplitMix64: a small, fast, fully deterministic pseudo-random generator.

#include <cstdint>

namespace Comprendo
{
  /// SplitMix64 generator (Steele, Lea, Flood 2014), as used to seed xoshiro.
  ///
  /// The sequence depends only on the seed, so layouts seeded from node ids are
  /// reproducible on every platform.
  class SplitMix64
  {
  public:
    /// Start a sequence.
    ///
    /// \param seed Initial state.
    explicit constexpr SplitMix64(std::uint64_t seed) : m_state(seed) { }

    /// Produce the next 64-bit value.
    ///
    /// \return Next value of the sequence.
    constexpr std::uint64_t
    next()
    {
      m_state += 0x9e3779b97f4a7c15ULL;
      std::uint64_t z = m_state;
      z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
      z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
      return z ^ (z >> 31U);
    }

    /// Produce a uniform double in [0, 1) from the top 53 bits.
    ///
    /// \return Next value scaled to [0, 1).
    constexpr double
    next_unit()
    {
      constexpr double kScale = 1.0 / static_cast<double>(1ULL << 53U);
      return static_cast<double>(next() >> 11U) * kScale;
    }

  private:
    /// Current state.
    std::uint64_t m_state;
  };
} // namespace Comprendo
