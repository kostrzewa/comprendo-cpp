/// \file
/// \brief Tests for the SplitMix64 generator.

#include <catch2/catch_test_macros.hpp>

#include "Comprendo/Random.hpp"

/// Layout positions are seeded with SplitMix64, so the generator must match the
/// reference algorithm exactly or golden layouts would drift between platforms.
/// Draws the first three values from seed 0 and compares them with the published
/// reference outputs, then checks that equal seeds give equal sequences and that
/// next_unit() stays in [0, 1) over many draws.
TEST_CASE("splitmix64 matches the reference sequence", "[random]")
{
  Comprendo::SplitMix64 random {0};
  CHECK(random.next() == 0xe220a8397b1dcdafULL);
  CHECK(random.next() == 0x6e789e6aa1b965f4ULL);
  CHECK(random.next() == 0x06c45d188009454fULL);

  Comprendo::SplitMix64 first {42};
  Comprendo::SplitMix64 second {42};
  bool same = true;
  bool in_range = true;
  for (int i = 0; i < 10000; ++i)
  {
    same = same && first.next() == second.next();
    const double unit = first.next_unit();
    in_range = in_range && unit >= 0.0 && unit < 1.0;
    static_cast<void>(second.next_unit());
  }
  CHECK(same);
  CHECK(in_range);
}
