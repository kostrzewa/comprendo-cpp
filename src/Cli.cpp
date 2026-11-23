/// \file
/// \brief The `comprendo-cli` command dispatcher: `extract`, `validate`, `info`.

#include "Comprendo/Cli.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include "Comprendo/Chart.hpp"
#include "Comprendo/ChartStore.hpp"
#include "Comprendo/ClangdExtractor.hpp"
#include "Comprendo/CtagsExtractor.hpp"
#include "Comprendo/ExtractConfig.hpp"
#include "Profiler/Profiler.hpp"
#include "Version.hpp"

namespace
{
  using Comprendo::kExitFailure;
  using Comprendo::kExitOk;
  using Comprendo::kExitUsage;

  /// Options of the `extract` command.
  struct ExtractOptions
  {
    /// Project root to extract.
    std::string root = ".";
    /// Chart file to write; defaults to the root's `.comprendo/chart.sqlite`.
    std::optional<std::string> output;
    /// Backend overriding the configuration.
    std::optional<std::string> backend;
  };

  /// Run `extract`.
  ///
  /// \param options Parsed command options.
  /// \param out Result stream.
  /// \param err Diagnostic stream.
  /// \return Exit code.
  int
  run_extract(const ExtractOptions &options, std::ostream &out, std::ostream &err)
  {
    const std::filesystem::path root = options.root;
    const std::filesystem::path chart_path
      = options.output
          .transform([](const std::string &path) { return std::filesystem::path {path}; })
          .value_or(root / Comprendo::kDefaultChartPath)
          .lexically_normal();

    auto config = Comprendo::load_extract_config(root);
    if (!config)
    {
      fmt::print(err, "comprendo-cli: {}\n", config.error());
      return kExitFailure;
    }
    if (options.backend)
    {
      const auto backend = Comprendo::parse_extract_backend(*options.backend);
      if (!backend)
      {
        fmt::print(err, "comprendo-cli: unknown backend '{}'\n", *options.backend);
        return kExitUsage;
      }
      config->backend = *backend;
    }
    std::unique_ptr<Comprendo::Extractor> extractor;
    if (config->backend == Comprendo::ExtractBackend::Ctags)
    {
      extractor = std::make_unique<Comprendo::CtagsExtractor>();
    }
    else
    {
      extractor = std::make_unique<Comprendo::ClangdExtractor>(
        config->backend == Comprendo::ExtractBackend::Clangd
      );
    }
    auto chart = extractor->extract(*config);
    if (!chart)
    {
      fmt::print(err, "comprendo-cli: extraction failed: {}\n", chart.error());
      return kExitFailure;
    }
    if (auto written = Comprendo::write_chart(chart_path, *chart); !written)
    {
      fmt::print(err, "comprendo-cli: {}\n", written.error());
      return kExitFailure;
    }
    fmt::print(
      out,
      "wrote {} ({} nodes, {} edges)\n",
      chart_path.string(),
      chart->nodes.size(),
      chart->edges.size()
    );
    return kExitOk;
  }

  /// Run `validate`.
  ///
  /// \param path Chart file to check.
  /// \param out Result stream.
  /// \param err Diagnostic stream.
  /// \return Exit code.
  int
  run_validate(const std::filesystem::path &path, std::ostream &out, std::ostream &err)
  {
    const Comprendo::ValidationResult result = Comprendo::check_chart_file(path);
    if (!result.ok())
    {
      fmt::print(err, "{}: invalid chart\n", path.string());
      for (const std::string &error : result.errors)
      {
        fmt::print(err, "  - {}\n", error);
      }
      return kExitFailure;
    }
    fmt::print(out, "{}: valid\n", path.string());
    return kExitOk;
  }

  /// Print per-kind counts of nodes or edges, skipping kinds that do not occur.
  ///
  /// \param out Result stream.
  /// \param counts Counts indexed by kind value.
  /// \param name Function mapping a kind value to its name.
  template <std::size_t N, typename Name>
  void
  print_counts(std::ostream &out, const std::array<std::size_t, N> &counts, Name name)
  {
    for (std::size_t value = 0; value < N; ++value)
    {
      if (counts.at(value) > 0)
      {
        fmt::print(out, "  {:<12} {}\n", name(static_cast<std::uint8_t>(value)), counts.at(value));
      }
    }
  }

  /// Run `info`.
  ///
  /// \param path Chart file to summarize.
  /// \param out Result stream.
  /// \param err Diagnostic stream.
  /// \return Exit code.
  int
  run_info(const std::filesystem::path &path, std::ostream &out, std::ostream &err)
  {
    const auto chart = Comprendo::read_chart(path);
    if (!chart)
    {
      fmt::print(err, "comprendo-cli: {}\n", chart.error());
      return kExitFailure;
    }

    constexpr std::size_t kNodeKinds = 14;
    constexpr std::size_t kEdgeKinds = 7;
    std::array<std::size_t, kNodeKinds> node_counts {};
    std::array<std::size_t, kEdgeKinds> edge_counts {};
    for (const Comprendo::Node &node : chart->nodes)
    {
      if (const std::size_t kind = std::to_underlying(node.kind); kind < kNodeKinds)
      {
        ++node_counts.at(kind);
      }
    }
    for (const Comprendo::Edge &edge : chart->edges)
    {
      if (const std::size_t kind = std::to_underlying(edge.kind); kind < kEdgeKinds)
      {
        ++edge_counts.at(kind);
      }
    }

    fmt::print(out, "chart:        {}\n", path.string());
    fmt::print(out, "schema:       {}\n", chart->schema_version);
    fmt::print(out, "project:      {}\n", chart->project);
    fmt::print(out, "git commit:   {}\n", chart->git_commit);
    fmt::print(out, "generated at: {}\n", chart->generated_at);
    fmt::print(out, "generator:    {}\n", chart->generator);
    fmt::print(out, "source root:  {}\n", chart->source_root);
    fmt::print(out, "nodes:        {}\n", chart->nodes.size());
    print_counts(
      out,
      node_counts,
      [](std::uint8_t kind) { return Comprendo::to_string(static_cast<Comprendo::NodeKind>(kind)); }
    );
    fmt::print(out, "edges:        {}\n", chart->edges.size());
    print_counts(
      out,
      edge_counts,
      [](std::uint8_t kind) { return Comprendo::to_string(static_cast<Comprendo::EdgeKind>(kind)); }
    );
    fmt::print(out, "clusters:     {}\n", chart->clusters.size());
    fmt::print(
      out,
      "layout:       {}\n",
      chart->layout.empty() ? std::string {"none"}
                            : fmt::format("{} positions", chart->layout.size())
    );
    return kExitOk;
  }
} // namespace

namespace Comprendo
{
  int
  run_cli(std::span<const std::string_view> args, std::ostream &out, std::ostream &err)
  {
    CLI::App app {"Extract, validate, and summarize Comprendo code charts.", "comprendo-cli"};
    app.set_version_flag(
      "--version", fmt::format("comprendo-cli {} (git {})", COMPRENDO_VERSION, COMPRENDO_GIT_COMMIT)
    );
    app.require_subcommand(1);
    std::string profile;
    app.add_option(
      "--profile", profile, "Write a Perfetto trace to FILE and log a profile at exit"
    );
    app.footer("Exit codes: 0 success, 1 failure, 2 malformed command line.");

    ExtractOptions extract_options;
    CLI::App *extract
      = app.add_subcommand("extract", "Extract the chart with Universal Ctags and clangd");
    extract->add_option("--root", extract_options.root, "Project root to extract")
      ->capture_default_str();
    extract->add_option(
      "--output",
      extract_options.output,
      "Chart file to write [default: ROOT/.comprendo/chart.sqlite]"
    );
    extract
      ->add_option(
        "--backend",
        extract_options.backend,
        "auto (clangd when available), ctags, or clangd [default: from config, else auto]"
      )
      ->check(CLI::IsMember({"auto", "ctags", "clangd"}));

    std::string validate_path {kDefaultChartPath};
    CLI::App *validate = app.add_subcommand("validate", "Check a chart; exit 1 when it is invalid");
    validate->add_option("file", validate_path, "Chart file")->capture_default_str();

    std::string info_path {kDefaultChartPath};
    CLI::App *info = app.add_subcommand("info", "Summarize a chart");
    info->add_option("file", info_path, "Chart file")->capture_default_str();

    std::vector<std::string> reversed(args.rbegin(), args.rend());
    try
    {
      app.parse(reversed);
    }
    catch (const CLI::RequiredError &error)
    {
      const std::vector<std::string> unknown = app.remaining();
      if (unknown.empty())
      {
        return app.exit(error, out, err) == 0 ? kExitOk : kExitUsage;
      }
      fmt::print(
        err, "Unknown command '{}'\nRun with --help for more information.\n", unknown.front()
      );
      return kExitUsage;
    }
    catch (const CLI::ParseError &error)
    {
      return app.exit(error, out, err) == 0 ? kExitOk : kExitUsage;
    }

    const Comprendo::ProfileRecording recording {profile};
    if (extract->parsed())
    {
      return run_extract(extract_options, out, err);
    }
    if (validate->parsed())
    {
      return run_validate(validate_path, out, err);
    }
    return run_info(info_path, out, err);
  }
} // namespace Comprendo
