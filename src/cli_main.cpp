/// \file
/// \brief Entry point for the Comprendo command-line tool.

#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "Comprendo/Cli.hpp"
#include "Logger/Logger.hpp"

/// Program entry point.
///
/// Logs go to standard error so standard output carries only command results.
///
/// \param argc Number of command-line arguments.
/// \param argv Command-line argument vector.
/// \return Process exit code (see Comprendo::ExitCode).
int
main(int argc, char **argv)
{
  Comprendo::Logger::init(
    "info", "", Comprendo::Logger::kDefaultPattern, Comprendo::Logger::ConsoleStream::Stderr
  );
  const std::span<char *> raw {argv, static_cast<std::size_t>(argc)};
  const std::span<char *> rest = raw.empty() ? raw : raw.subspan(1);
  const std::vector<std::string_view> args(rest.begin(), rest.end());
  return Comprendo::run_cli(args, std::cout, std::cerr);
}
