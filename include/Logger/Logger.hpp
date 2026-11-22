#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>

#include "qcd/Version.hpp"

namespace qcd::Logger
{
  /// Exception thrown by Logger::error().
  struct LoggerError : std::runtime_error
  {
    /// Construct the exception from a formatted message.
    explicit LoggerError(const std::string &msg) : std::runtime_error("Logger::error: " + msg) { }
  };

  /// Initialize the global logger.
  ///
  /// \param level   One of "debug", "info", "warn", "error" (default "info").
  /// \param log_file Optional file sink; empty means console only.
  /// \param pattern spdlog pattern used for every sink.
  ///
  /// The startup banner always contains the project version and the git commit
  /// hash so every log file can be traced back to a revision.
  inline void
  init(
    std::string level = "info",
    std::string log_file = "",
    std::string pattern = "[%D %T.%e] [%^%=7l%$] %v"
  )
  {
    const std::vector<std::pair<std::string, spdlog::level::level_enum>> levels = {
      {"debug", spdlog::level::debug},
      {"info", spdlog::level::info},
      {"warn", spdlog::level::warn},
      {"error", spdlog::level::err},
      {"", spdlog::level::info}
    };

    spdlog::level::level_enum selected = spdlog::level::info;
    for (const auto &[name, value] : levels)
    {
      if (name == level)
      {
        selected = value;
        break;
      }
    }

    std::vector<spdlog::sink_ptr> sinks;
    if (!log_file.empty())
    {
      auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
      file_sink->set_level(selected);
      sinks.push_back(file_sink);
    }

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(selected);
    sinks.push_back(console_sink);

    auto logger = std::make_shared<spdlog::logger>("qcd_logger", sinks.begin(), sinks.end());
    logger->set_pattern(pattern);
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::debug);

    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);

    spdlog::info("QCD Game v{} (git {})", QCD_VERSION, QCD_GIT_COMMIT);
  }

  /// Log a debug message.
  template <typename... Args>
  inline void
  debug(fmt::format_string<Args...> fmt, Args &&...args)
  {
    spdlog::debug(fmt, std::forward<Args>(args)...);
  }

  /// Log an informational message.
  template <typename... Args>
  inline void
  info(fmt::format_string<Args...> fmt, Args &&...args)
  {
    spdlog::info(fmt, std::forward<Args>(args)...);
  }

  /// Log a warning.
  template <typename... Args>
  inline void
  warn(fmt::format_string<Args...> fmt, Args &&...args)
  {
    spdlog::warn(fmt, std::forward<Args>(args)...);
  }

  /// Log an error and throw LoggerError.
  template <typename... Args>
  [[noreturn]]
  inline void
  error(fmt::format_string<Args...> fmt, Args &&...args)
  {
    spdlog::error(fmt, std::forward<Args>(args)...);
    throw LoggerError(fmt::format(fmt, std::forward<Args>(args)...));
  }

} // namespace qcd::Logger
