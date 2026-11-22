/// \file
/// \brief Entry point for the Comprendo command-line tool.

#include <string_view>

#include "Logger/Logger.hpp"
#include "Version.hpp"

namespace
{
  /// Print command-line usage to standard output.
  void
  print_usage()
  {
    Comprendo::Logger::info("Usage: comprendo-cli [--version] [--help]");
    Comprendo::Logger::info("  --version  Print version and git commit");
    Comprendo::Logger::info("  --help     Print this message");
  }
} // namespace

/// Program entry point.
///
/// \param argc Number of command-line arguments.
/// \param argv Command-line argument vector.
/// \return Process exit code.
int
main(int argc, char **argv)
{
  Comprendo::Logger::init();

  for (int i = 1; i < argc; ++i)
  {
    const std::string_view arg{argv[i]};
    if (arg == "--version")
    {
      Comprendo::Logger::info("Comprendo {} (git {})", COMPRENDO_VERSION, COMPRENDO_GIT_COMMIT);
      return 0;
    }
    if (arg == "--help")
    {
      print_usage();
      return 0;
    }

    Comprendo::Logger::warn("Unknown argument: {}", arg);
    print_usage();
    return 1;
  }

  print_usage();
  return 0;
}
