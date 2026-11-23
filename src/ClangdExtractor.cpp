/// \file
/// \brief clangd-22 enrichment: call and inheritance edges resolved semantically.

#include "Comprendo/ClangdExtractor.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "Comprendo/CtagsExtractor.hpp"
#include "Comprendo/DocComments.hpp"
#include "Comprendo/Json.hpp"
#include "Comprendo/Lsp.hpp"
#include "Comprendo/Process.hpp"
#include "Logger/Logger.hpp"
#include "Profiler/Profiler.hpp"
#include "Version.hpp"

namespace
{
  using Comprendo::Chart;
  using Comprendo::Json;
  using Comprendo::NodeKind;

  /// Name of the compilation database file.
  constexpr std::string_view kCompileCommands = "compile_commands.json";

  /// Longest wait for one clangd response (parsing a large file can be slow).
  constexpr std::chrono::milliseconds kRequestTimeout {120000};

  /// Queries between two progress messages.
  constexpr std::size_t kProgressInterval = 250;

  /// File extensions opened in clangd, with their LSP language ids.
  constexpr std::array<std::pair<std::string_view, std::string_view>, 14> kSourceLanguages = {{
    {".c", "c"},
    {".h", "cpp"},
    {".cc", "cpp"},
    {".cpp", "cpp"},
    {".cxx", "cpp"},
    {".c++", "cpp"},
    {".hh", "cpp"},
    {".hpp", "cpp"},
    {".hxx", "cpp"},
    {".h++", "cpp"},
    {".inl", "cpp"},
    {".ipp", "cpp"},
    {".cu", "cuda"},
    {".cuh", "cuda"},
  }};

  /// Kinds whose outgoing calls are queried and which calls may target.
  constexpr std::array<NodeKind, 2> kCallableKinds = {NodeKind::Function, NodeKind::Method};

  /// Kinds whose supertypes are queried and which inheritance may target.
  constexpr std::array<NodeKind, 3> kTypeKinds
    = {NodeKind::Class, NodeKind::Struct, NodeKind::Union};

  /// Kinds whose references become `Uses` edges.
  constexpr std::array<NodeKind, 7> kUsedKinds = {
    NodeKind::Class,
    NodeKind::Struct,
    NodeKind::Union,
    NodeKind::Enum,
    NodeKind::TypeAlias,
    NodeKind::Field,
    NodeKind::Variable,
  };

  /// Kinds that can contain a reference: the innermost one is the user.
  constexpr std::array<NodeKind, 5> kContainerKinds
    = {NodeKind::Function, NodeKind::Method, NodeKind::Class, NodeKind::Struct, NodeKind::Union};

  /// Check whether a byte can continue a C/C++ identifier.
  ///
  /// \param c Byte.
  /// \return True for letters, digits, and '_'.
  bool
  is_identifier_byte(char c)
  {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
  }

  /// LSP language id of a file, by extension.
  ///
  /// \param file Repository-relative path.
  /// \return Language id, or nothing for files clangd should not open.
  std::optional<std::string_view>
  language_of(std::string_view file)
  {
    std::string extension = std::filesystem::path {file}.extension().string();
    std::ranges::transform(
      extension, extension.begin(), [](unsigned char c) { return std::tolower(c); }
    );
    const auto found = std::ranges::find(
      kSourceLanguages, extension, &std::pair<std::string_view, std::string_view>::first
    );
    return found == kSourceLanguages.end() ? std::nullopt : std::optional {found->second};
  }

  /// Build `{"textDocument": {"uri": ...}, "position": {...}}`.
  ///
  /// \param uri Document URI.
  /// \param line 0-based line.
  /// \param column 0-based byte column.
  /// \return Request parameters.
  Json
  position_params(const std::string &uri, std::uint32_t line, std::size_t column)
  {
    return Json {Json::Object {
      {"textDocument", Json {Json::Object {{"uri", uri}}}},
      {"position",
       Json {Json::Object {
         {"line", static_cast<std::int64_t>(line)}, {"character", static_cast<std::int64_t>(column)}
       }}},
    }};
  }

  /// 0-based start line of an LSP range.
  ///
  /// \param range Range object.
  /// \return Line, or nothing when malformed.
  std::optional<std::uint32_t>
  range_start_line(const Json &range)
  {
    const Json *start = range.find("start");
    const Json *line = start == nullptr ? nullptr : start->find("line");
    const double *value = line == nullptr ? nullptr : line->as_number();
    return value == nullptr || *value < 0 ? std::nullopt
                                          : std::optional {static_cast<std::uint32_t>(*value)};
  }

  /// 0-based start line of a hierarchy item's selection range (its name).
  ///
  /// \param item Call or type hierarchy item.
  /// \return Line, or nothing when malformed.
  std::optional<std::uint32_t>
  selection_line(const Json &item)
  {
    const Json *range = item.find("selectionRange");
    return range == nullptr ? std::nullopt : range_start_line(*range);
  }

  /// An edge found by clangd, keyed for deterministic merging.
  struct FoundEdge
  {
    /// Number of call sites (1 for inheritance).
    float weight = 0.0F;
    /// File:line of the first call site or of the derived class.
    std::string evidence;
  };

  /// State of one clangd session over a chart.
  class Session
  {
  public:
    /// Prepare a session.
    ///
    /// \param chart Chart to enrich.
    /// \param root Absolute, canonical project root.
    /// \param client Running, initialized clangd.
    Session(const Chart &chart, std::filesystem::path root, Comprendo::LspClient &client) :
      m_chart(&chart), m_root(std::move(root)), m_client(&client), m_locator(chart)
    {
    }

    /// Open every C/C++ file of the chart and wait until clangd has parsed it.
    ///
    /// \return Nothing, or an error message when the connection fails.
    std::expected<void, std::string>
    open_files()
    {
      const Comprendo::ProfileScope profile {"clangd_open"};
      for (const Comprendo::Node &node : m_chart->nodes)
      {
        const auto language = language_of(node.file);
        if (node.kind != NodeKind::File || !language)
        {
          continue;
        }
        std::ifstream stream {m_root / node.file, std::ios::binary};
        std::string text {std::istreambuf_iterator<char> {stream}, {}};
        const std::string uri = Comprendo::path_to_uri(m_root / node.file);
        auto opened = m_client->notify(
          "textDocument/didOpen",
          Json {Json::Object {
            {"textDocument",
             Json {Json::Object {
               {"uri", uri},
               {"languageId", *language},
               {"version", 1},
               {"text", text},
             }}}
          }}
        );
        if (!opened)
        {
          return std::unexpected(opened.error());
        }
        m_lines[node.file] = Comprendo::split_source_lines(text);
        m_uris[node.file] = uri;
      }
      for (const auto &[file, uri] : m_uris)
      {
        auto parsed = m_client->request(
          "textDocument/documentSymbol",
          Json {Json::Object {{"textDocument", Json {Json::Object {{"uri", uri}}}}}}
        );
        if (!parsed)
        {
          Comprendo::Logger::debug("clangd could not parse {}: {}", file, parsed.error());
        }
      }
      // clangd's `sync` extension waits for asynchronous preamble indexing; without
      // it, calls to symbols declared in headers may be missing nondeterministically.
      if (auto idle = m_client->request("sync", Json {}); !idle)
      {
        Comprendo::Logger::debug("clangd sync unavailable: {}", idle.error());
      }
      Comprendo::Logger::info("clangd parsed {} files", m_uris.size());
      return {};
    }

    /// Query call and type hierarchies for every function, method, and type.
    ///
    /// \return Nothing, or an error message when the connection fails.
    std::expected<void, std::string>
    query()
    {
      const Comprendo::ProfileScope profile {"clangd_query"};
      std::vector<std::size_t> order;
      for (std::size_t index = 0; index < m_chart->nodes.size(); ++index)
      {
        const Comprendo::Node &node = m_chart->nodes[index];
        const bool queried_kind = std::ranges::contains(kCallableKinds, node.kind)
          || std::ranges::contains(kUsedKinds, node.kind);
        if (queried_kind && m_uris.contains(node.file))
        {
          order.push_back(index);
        }
      }
      // clangd retains only a few ASTs; visiting one file at a time avoids reparsing.
      std::ranges::sort(
        order,
        [&](std::size_t a, std::size_t b)
        {
          const Comprendo::Node &left = m_chart->nodes[a];
          const Comprendo::Node &right = m_chart->nodes[b];
          return std::tie(left.file, left.line_start, a)
            < std::tie(right.file, right.line_start, b);
        }
      );
      std::size_t queries = 0;
      for (const std::size_t index : order)
      {
        const NodeKind kind = m_chart->nodes[index].kind;
        auto queried = std::ranges::contains(kCallableKinds, kind) ? outgoing_calls(index)
          : std::ranges::contains(kTypeKinds, kind) ? supertypes(index)
                                                    : std::expected<void, std::string> {};
        if (queried && std::ranges::contains(kUsedKinds, kind))
        {
          queried = references(index);
        }
        if (!queried)
        {
          return queried;
        }
        if (++queries % kProgressInterval == 0)
        {
          Comprendo::Logger::info("clangd: {} symbols queried", queries);
        }
      }
      return {};
    }

    /// Edges found, ordered by (source id, destination id, kind).
    ///
    /// \return Found edges.
    [[nodiscard]]
    const std::map<std::tuple<std::uint64_t, std::uint64_t, Comprendo::EdgeKind>, FoundEdge> &
    edges() const
    {
      return m_edges;
    }

  private:
    /// Prepare a hierarchy item for a node.
    ///
    /// \param index Node index.
    /// \param method `textDocument/prepareCallHierarchy` or `.../prepareTypeHierarchy`.
    /// \return The first item, nothing when clangd finds none, or an error message.
    std::expected<std::optional<Json>, std::string>
    prepare(std::size_t index, std::string_view method)
    {
      const Comprendo::Node &node = m_chart->nodes[index];
      const std::vector<std::string> &lines = m_lines.at(node.file);
      if (node.line_start == 0 || node.line_start > lines.size())
      {
        return std::optional<Json> {};
      }
      const auto column = Comprendo::find_name_column(lines[node.line_start - 1], node.name);
      if (!column)
      {
        return std::optional<Json> {};
      }
      auto items = m_client->request(
        method, position_params(m_uris.at(node.file), node.line_start - 1, *column)
      );
      if (!items)
      {
        return std::unexpected(items.error());
      }
      const Json::Array *array = items->as_array();
      return array == nullptr || array->empty() ? std::optional<Json> {}
                                                : std::optional<Json> {array->front()};
    }

    /// Map a hierarchy item to a node.
    ///
    /// \param item Call or type hierarchy item.
    /// \param kinds Accepted node kinds.
    /// \return Node index, or nothing for symbols outside the chart.
    std::optional<std::size_t>
    resolve(const Json &item, std::span<const NodeKind> kinds) const
    {
      const auto file = project_path(item.string_or_empty("uri"));
      const auto line = selection_line(item);
      if (!file || !line)
      {
        return std::nullopt;
      }
      return m_locator.locate(*file, *line + 1, item.string_or_empty("detail"), kinds);
    }

    /// Convert a file URI to a repository-relative path.
    ///
    /// \param uri File URI reported by clangd.
    /// \return The path, or nothing for files outside the project.
    std::optional<std::string>
    project_path(std::string_view uri) const
    {
      const auto path = Comprendo::uri_to_path(uri);
      if (!path)
      {
        return std::nullopt;
      }
      const std::filesystem::path relative
        = std::filesystem::weakly_canonical(*path).lexically_relative(m_root);
      if (relative.empty() || *relative.begin() == "..")
      {
        return std::nullopt;
      }
      return relative.generic_string();
    }

    /// Record which functions and types use a type, field, or variable.
    ///
    /// Each reference outside the symbol's own definition becomes a `Uses`
    /// edge from the innermost function, method, or class containing it,
    /// except from the symbol's own members (`Circle::area` naming `Circle`
    /// in its qualifier). Base-class specifiers are dropped later, since the
    /// `Inherits` edge already covers them.
    ///
    /// \param index Node index.
    /// \return Nothing, or an error message.
    std::expected<void, std::string>
    references(std::size_t index)
    {
      const Comprendo::Node &node = m_chart->nodes[index];
      const std::vector<std::string> &lines = m_lines.at(node.file);
      if (node.line_start == 0 || node.line_start > lines.size())
      {
        return {};
      }
      const auto column = Comprendo::find_name_column(lines[node.line_start - 1], node.name);
      if (!column)
      {
        return {};
      }
      const Json position = position_params(m_uris.at(node.file), node.line_start - 1, *column);
      Json::Object params = *position.as_object();
      params.emplace_back("context", Json {Json::Object {{"includeDeclaration", false}}});
      auto found = m_client->request("textDocument/references", Json {std::move(params)});
      if (!found)
      {
        return std::unexpected(found.error());
      }
      const Json::Array *locations = found->as_array();
      if (locations == nullptr)
      {
        return {};
      }
      for (const Json &location : *locations)
      {
        const auto file = project_path(location.string_or_empty("uri"));
        const Json *range = location.find("range");
        const auto line = range == nullptr ? std::nullopt : range_start_line(*range);
        if (!file || !line)
        {
          continue;
        }
        const std::uint32_t at = *line + 1;
        if (*file == node.file && at >= node.line_start && at <= node.line_end)
        {
          continue;
        }
        const auto user = m_locator.locate(*file, at, "", kContainerKinds);
        const bool member_of_target
          = user && m_chart->nodes[*user].qualified_name.starts_with(node.qualified_name + "::");
        if (user && *user != index && !member_of_target)
        {
          add_edge(*user, index, Comprendo::EdgeKind::Uses, 1.0F, fmt::format("{}:{}", *file, at));
        }
      }
      return {};
    }

    /// Record an edge, merging repeated ones.
    ///
    /// \param from Source node index.
    /// \param to Destination node index.
    /// \param kind Edge kind.
    /// \param weight Weight to add.
    /// \param evidence Evidence kept from the first occurrence.
    void
    add_edge(
      std::size_t from, std::size_t to, Comprendo::EdgeKind kind, float weight, std::string evidence
    )
    {
      const auto key = std::tuple {m_chart->nodes[from].id, m_chart->nodes[to].id, kind};
      auto [entry, inserted]
        = m_edges.try_emplace(key, FoundEdge {.weight = 0.0F, .evidence = std::move(evidence)});
      entry->second.weight += weight;
    }

    /// Record the outgoing calls of a function or method.
    ///
    /// \param index Node index.
    /// \return Nothing, or an error message.
    std::expected<void, std::string>
    outgoing_calls(std::size_t index)
    {
      auto item = prepare(index, "textDocument/prepareCallHierarchy");
      if (!item || !*item)
      {
        return item ? std::expected<void, std::string> {} : std::unexpected(item.error());
      }
      auto calls
        = m_client->request("callHierarchy/outgoingCalls", Json {Json::Object {{"item", **item}}});
      if (!calls)
      {
        return std::unexpected(calls.error());
      }
      const Json::Array *array = calls->as_array();
      if (array == nullptr)
      {
        return {};
      }
      const std::string &file = m_chart->nodes[index].file;
      for (const Json &call : *array)
      {
        const Json *to = call.find("to");
        const Json *ranges = call.find("fromRanges");
        const auto target = to == nullptr ? std::nullopt : resolve(*to, kCallableKinds);
        if (!target || *target == index)
        {
          continue;
        }
        const Json::Array *sites = ranges == nullptr ? nullptr : ranges->as_array();
        const std::size_t count = sites == nullptr ? 1 : std::max<std::size_t>(sites->size(), 1);
        const auto first
          = sites == nullptr || sites->empty() ? std::nullopt : range_start_line(sites->front());
        const std::uint32_t line = first ? *first + 1 : m_chart->nodes[index].line_start;
        add_edge(
          index,
          *target,
          Comprendo::EdgeKind::Calls,
          static_cast<float>(count),
          fmt::format("{}:{}", file, line)
        );
      }
      return {};
    }

    /// Record the direct supertypes of a class, struct, or union.
    ///
    /// \param index Node index.
    /// \return Nothing, or an error message.
    std::expected<void, std::string>
    supertypes(std::size_t index)
    {
      auto item = prepare(index, "textDocument/prepareTypeHierarchy");
      if (!item || !*item)
      {
        return item ? std::expected<void, std::string> {} : std::unexpected(item.error());
      }
      auto bases
        = m_client->request("typeHierarchy/supertypes", Json {Json::Object {{"item", **item}}});
      if (!bases)
      {
        return std::unexpected(bases.error());
      }
      const Json::Array *array = bases->as_array();
      if (array == nullptr)
      {
        return {};
      }
      const Comprendo::Node &node = m_chart->nodes[index];
      for (const Json &base : *array)
      {
        const auto target = resolve(base, kTypeKinds);
        if (target && *target != index)
        {
          add_edge(
            index,
            *target,
            Comprendo::EdgeKind::Inherits,
            1.0F,
            fmt::format("{}:{}", node.file, node.line_start)
          );
        }
      }
      return {};
    }

    /// Chart whose symbols are queried.
    const Chart *m_chart;
    /// Canonical project root.
    std::filesystem::path m_root;
    /// clangd connection.
    Comprendo::LspClient *m_client;
    /// Position-to-node mapping.
    Comprendo::NodeLocator m_locator;
    /// Lines of every opened file.
    std::unordered_map<std::string, std::vector<std::string>> m_lines;
    /// URI of every opened file, ordered for deterministic parsing requests.
    std::map<std::string, std::string> m_uris;
    /// Edges found so far.
    std::map<std::tuple<std::uint64_t, std::uint64_t, Comprendo::EdgeKind>, FoundEdge> m_edges;
  };
} // namespace

namespace Comprendo
{
  std::optional<std::size_t>
  find_name_column(std::string_view line, std::string_view name)
  {
    std::string_view needle = name;
    if (needle.starts_with("operator"))
    {
      needle = "operator";
    }
    if (needle.empty())
    {
      return std::nullopt;
    }
    std::optional<std::size_t> first;
    for (std::size_t at = line.find(needle); at != std::string_view::npos;
         at = line.find(needle, at + 1))
    {
      const std::size_t end = at + needle.size();
      const bool starts_word
        = at == 0 || !is_identifier_byte(line[at - 1]) || needle.front() == '~';
      const bool ends_word = end >= line.size() || !is_identifier_byte(line[end]);
      if (!starts_word || !ends_word)
      {
        continue;
      }
      if (!first)
      {
        first = at;
      }
      const std::size_t next = line.find_first_not_of(" \t", end);
      if (next != std::string_view::npos && line[next] == '(')
      {
        return at;
      }
    }
    return first;
  }

  std::optional<std::filesystem::path>
  find_compile_commands(const std::filesystem::path &root, std::string_view configured)
  {
    const auto has_database = [](const std::filesystem::path &dir)
    { return std::filesystem::is_regular_file(dir / kCompileCommands); };
    if (!configured.empty())
    {
      const std::filesystem::path dir = root / configured;
      return has_database(dir) ? std::optional {dir} : std::nullopt;
    }
    for (const std::filesystem::path &dir : {root, root / "build"})
    {
      if (has_database(dir))
      {
        return dir;
      }
    }
    std::vector<std::filesystem::path> candidates;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator {root / "build", error})
    {
      if (entry.is_directory(error) && has_database(entry.path()))
      {
        candidates.push_back(entry.path());
      }
    }
    std::ranges::sort(candidates);
    return candidates.empty() ? std::nullopt : std::optional {candidates.front()};
  }

  NodeLocator::NodeLocator(const Chart &chart) : m_chart(&chart)
  {
    for (std::size_t i = 0; i < chart.nodes.size(); ++i)
    {
      m_by_file[chart.nodes[i].file].push_back(i);
      m_by_name[chart.nodes[i].qualified_name].push_back(i);
    }
  }

  std::optional<std::size_t>
  NodeLocator::locate(
    std::string_view file,
    std::uint32_t line,
    std::string_view qualified_name,
    std::span<const NodeKind> kinds
  ) const
  {
    std::optional<std::size_t> best;
    std::uint32_t best_span = 0;
    if (const auto nodes = m_by_file.find(std::string {file}); nodes != m_by_file.end())
    {
      for (const std::size_t index : nodes->second)
      {
        const Node &node = m_chart->nodes[index];
        if (
          !std::ranges::contains(kinds, node.kind) || line < node.line_start || line > node.line_end
        )
        {
          continue;
        }
        const std::uint32_t span = node.line_end - node.line_start;
        if (!best || span < best_span)
        {
          best = index;
          best_span = span;
        }
      }
    }
    if (best || qualified_name.empty())
    {
      return best;
    }
    const auto named = m_by_name.find(std::string {qualified_name});
    if (named == m_by_name.end())
    {
      return std::nullopt;
    }
    std::optional<std::size_t> only;
    for (const std::size_t index : named->second)
    {
      if (std::ranges::contains(kinds, m_chart->nodes[index].kind))
      {
        if (only)
        {
          return std::nullopt;
        }
        only = index;
      }
    }
    return only;
  }

  std::expected<void, std::string>
  enrich_with_clangd(
    Chart &chart,
    const std::filesystem::path &root,
    const std::string &clangd,
    const std::filesystem::path &compile_commands
  )
  {
    const ProfileScope profile {"clangd_enrich"};
    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root);
    const unsigned workers = std::max(1U, std::thread::hardware_concurrency());
    auto client = LspClient::start(
      {clangd,
       fmt::format(
         "--compile-commands-dir={}", std::filesystem::weakly_canonical(compile_commands).string()
       ),
       "--background-index=false",
       "--log=error",
       fmt::format("-j={}", workers)},
      canonical_root,
      kRequestTimeout
    );
    if (!client)
    {
      return std::unexpected(client.error());
    }
    const Json capabilities {Json::Object {
      {"general", Json {Json::Object {{"positionEncodings", Json {Json::Array {"utf-8"}}}}}},
      {"textDocument",
       Json {Json::Object {
         {"callHierarchy", Json {Json::Object {}}}, {"typeHierarchy", Json {Json::Object {}}}
       }}},
    }};
    auto initialized = client->request(
      "initialize",
      Json {Json::Object {
        {"processId", nullptr},
        {"rootUri", path_to_uri(canonical_root)},
        {"capabilities", capabilities}
      }}
    );
    if (!initialized)
    {
      return std::unexpected(initialized.error());
    }
    if (auto sent = client->notify("initialized", Json {Json::Object {}}); !sent)
    {
      return std::unexpected(sent.error());
    }

    Session session {chart, canonical_root, *client};
    auto done = session.open_files().and_then([&] { return session.query(); });
    if (auto stopped = client->shutdown(); !stopped)
    {
      Logger::debug("clangd did not shut down cleanly: {}", stopped.error());
    }
    if (!done)
    {
      return done;
    }

    std::size_t calls = 0;
    std::size_t inherits = 0;
    std::size_t uses = 0;
    for (const auto &[key, found] : session.edges())
    {
      const auto &[src, dst, kind] = key;
      if (kind == EdgeKind::Uses && session.edges().contains({src, dst, EdgeKind::Inherits}))
      {
        continue;
      }
      chart.edges.push_back({
        .src = src,
        .dst = dst,
        .kind = kind,
        .weight = found.weight,
        .evidence = found.evidence,
        .provenance = Provenance::Extractor,
      });
      (kind == EdgeKind::Calls ? calls : kind == EdgeKind::Uses ? uses : inherits) += 1;
    }
    canonicalize(chart);
    Logger::info(
      "clangd: {} call edges, {} inheritance edges, {} use edges", calls, inherits, uses
    );
    return {};
  }

  std::expected<Chart, std::string>
  ClangdExtractor::extract(const ExtractConfig &config)
  {
    auto chart = CtagsExtractor {}.extract(config);
    if (!chart)
    {
      return chart;
    }
    const auto fail = [&](std::string message) -> std::expected<Chart, std::string>
    {
      if (m_required)
      {
        return std::unexpected(std::move(message));
      }
      Logger::warn("{}; keeping the ctags chart", message);
      return std::move(*chart);
    };
    const auto compile_commands = find_compile_commands(config.root, config.compile_commands_dir);
    if (!compile_commands)
    {
      return fail(fmt::format("no {} found for clangd", kCompileCommands));
    }
    const auto version = run_process({config.clangd, "--version"});
    if (!version || version->exit_code != 0)
    {
      return fail(fmt::format("cannot run clangd '{}'", config.clangd));
    }
    if (
      auto enriched = enrich_with_clangd(*chart, config.root, config.clangd, *compile_commands);
      !enriched
    )
    {
      return fail(fmt::format("clangd failed: {}", enriched.error()));
    }
    chart->generator = fmt::format("comprendo {} (ctags+clangd)", COMPRENDO_VERSION);
    return std::move(*chart);
  }
} // namespace Comprendo
