#pragma once

/// \file
/// \brief How many worker threads to use: one per physical core.

#include <filesystem>
#include <span>

namespace Comprendo
{
  /// Count the physical cores behind a set of logical CPUs.
  ///
  /// Reads `cpuN/topology/physical_package_id` and `cpuN/topology/core_id`
  /// below \p cpu_root and counts distinct (package, core) pairs, so SMT
  /// siblings (hyper-threads) of one core count once. CPUs without readable
  /// topology count as one core each.
  ///
  /// \param cpu_root Directory holding `cpuN/`, normally `/sys/devices/system/cpu`.
  /// \param cpus Logical CPU numbers to consider.
  /// \return Number of physical cores, at least 1.
  [[nodiscard]]
  unsigned
  count_physical_cores(const std::filesystem::path &cpu_root, std::span<const unsigned> cpus);

  /// Number of physical cores this process may run on.
  ///
  /// Considers the CPUs in the process's affinity mask (so `taskset` and
  /// container limits are respected) and counts their physical cores; falls
  /// back to `std::thread::hardware_concurrency()` when the topology cannot be
  /// read. Computed once.
  ///
  /// \return Worker thread count, at least 1.
  [[nodiscard]]
  unsigned
  physical_core_count();
} // namespace Comprendo
