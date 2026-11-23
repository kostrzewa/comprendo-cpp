/// \file
/// \brief How many worker threads to use: one per physical core.

#include "Comprendo/Threads.hpp"

#include <sched.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Logger/Logger.hpp"

namespace
{
  /// Read one integer from a sysfs file.
  ///
  /// \param path File path.
  /// \return The value, or nothing when the file is missing or malformed.
  std::optional<long>
  read_number(const std::filesystem::path &path)
  {
    std::ifstream stream {path};
    long value = 0;
    if (stream >> value)
    {
      return value;
    }
    return std::nullopt;
  }

  /// Logical CPUs in this process's affinity mask.
  ///
  /// \return CPU numbers, empty when the mask cannot be read.
  std::vector<unsigned>
  allowed_cpus()
  {
    cpu_set_t set;
    CPU_ZERO(&set);
    std::vector<unsigned> cpus;
    if (sched_getaffinity(0, sizeof(set), &set) != 0)
    {
      return cpus;
    }
    for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu)
    {
      if (CPU_ISSET(cpu, &set))
      {
        cpus.push_back(cpu);
      }
    }
    return cpus;
  }
} // namespace

namespace Comprendo
{
  unsigned
  count_physical_cores(const std::filesystem::path &cpu_root, std::span<const unsigned> cpus)
  {
    std::set<std::pair<long, long>> cores;
    long unknown = 0;
    for (const unsigned cpu : cpus)
    {
      const std::filesystem::path topology = cpu_root / ("cpu" + std::to_string(cpu)) / "topology";
      const auto package = read_number(topology / "physical_package_id");
      const auto core = read_number(topology / "core_id");
      if (package && core)
      {
        cores.emplace(*package, *core);
      }
      else
      {
        // Distinct negative keys: each unreadable CPU is its own core.
        cores.emplace(-1, --unknown);
      }
    }
    return std::max(1U, static_cast<unsigned>(cores.size()));
  }

  unsigned
  physical_core_count()
  {
    static const unsigned count = []
    {
      const std::vector<unsigned> cpus = allowed_cpus();
      const unsigned hardware = std::max(1U, std::thread::hardware_concurrency());
      if (cpus.empty())
      {
        return hardware;
      }
      const unsigned cores = count_physical_cores("/sys/devices/system/cpu", cpus);
      Logger::debug("{} physical cores for {} allowed CPUs", cores, cpus.size());
      return cores;
    }();
    return count;
  }
} // namespace Comprendo
