/// \file
/// \brief Tests for choosing the number of worker threads.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Comprendo/Threads.hpp"
#include "TempDir.hpp"

namespace
{
  /// Write a fake sysfs topology entry for one logical CPU.
  ///
  /// \param root Fake `/sys/devices/system/cpu`.
  /// \param cpu Logical CPU number.
  /// \param package Physical package id.
  /// \param core Core id within the package.
  void
  add_cpu(const std::filesystem::path &root, unsigned cpu, int package, int core)
  {
    const std::filesystem::path topology = root / ("cpu" + std::to_string(cpu)) / "topology";
    std::filesystem::create_directories(topology);
    std::ofstream {topology / "physical_package_id"} << package << "\n";
    std::ofstream {topology / "core_id"} << core << "\n";
  }
} // namespace

/// Worker threads should match physical cores: SMT siblings share a core's
/// execution units, so a thread per hardware thread oversubscribes the cores.
/// Builds a fake topology with two packages of two cores, each with two
/// hyper-threads, and checks all CPUs count as four cores, a subset (an
/// affinity mask) counts only its cores, and unreadable CPUs count one each.
TEST_CASE("physical cores are counted from the CPU topology", "[threads]")
{
  const Comprendo::Testing::TempDir dir {"cpu-topology"};
  for (unsigned cpu = 0; cpu < 8; ++cpu)
  {
    add_cpu(dir.path(), cpu, static_cast<int>(cpu / 4), static_cast<int>(cpu % 2));
  }
  const std::vector<unsigned> all = {0, 1, 2, 3, 4, 5, 6, 7};
  CHECK(Comprendo::count_physical_cores(dir.path(), all) == 4);
  CHECK(Comprendo::count_physical_cores(dir.path(), std::vector<unsigned> {0, 2}) == 1);
  CHECK(Comprendo::count_physical_cores(dir.path(), std::vector<unsigned> {0, 1, 4}) == 3);
  CHECK(Comprendo::count_physical_cores(dir.path(), std::vector<unsigned> {40, 41}) == 2);
  CHECK(Comprendo::count_physical_cores(dir.path(), std::vector<unsigned> {}) == 1);
  CHECK(Comprendo::physical_core_count() >= 1);
}
