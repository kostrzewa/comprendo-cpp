/// \file
/// \brief Universal Ctags extractor: symbol inventory, `Contains` and `Includes` edges.

#include "Comprendo/CtagsExtractor.hpp"

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
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include "Comprendo/DocComments.hpp"
#include "Comprendo/Files.hpp"
#include "Comprendo/Json.hpp"
#include "Comprendo/Process.hpp"
#include "Logger/Logger.hpp"
#include "Profiler/Profiler.hpp"
#include "Version.hpp"

namespace
{
  using Comprendo::CtagsSourceFile;
  using Comprendo::CtagsTag;
  using Comprendo::NodeKind;
  using Comprendo::ProfileScope;

  /// Languages whose files are scanned for `#include` and use "::" as scope separator.
  constexpr std::array<std::string_view, 4> kCFamily = {"C", "C++", "CUDA", "ObjectiveC"};

  /// ctags kinds that are never charted.
  constexpr std::array<std::string_view, 7> kSkippedKinds
    = {"local", "parameter", "label", "header", "file", "macroparam", "externvar"};

  /// Prefix of the names ctags invents for anonymous entities (lambdas, unnamed structs).
  constexpr std::string_view kAnonymousPrefix = "__anon";

  /// Long ctags kind names and the node kind they map to.
  constexpr std::array<std::pair<std::string_view, NodeKind>, 34> kKindTable = {{
    {"namespace", NodeKind::Namespace}, {"module", NodeKind::Module},
    {"package", NodeKind::Module},      {"crate", NodeKind::Module},
    {"class", NodeKind::Class},         {"interface", NodeKind::Class},
    {"trait", NodeKind::Class},         {"implementation", NodeKind::Class},
    {"struct", NodeKind::Struct},       {"union", NodeKind::Union},
    {"enum", NodeKind::Enum},           {"function", NodeKind::Function},
    {"prototype", NodeKind::Function},  {"func", NodeKind::Function},
    {"subroutine", NodeKind::Function}, {"procedure", NodeKind::Function},
    {"method", NodeKind::Method},       {"singletonMethod", NodeKind::Method},
    {"member", NodeKind::Field},        {"field", NodeKind::Field},
    {"property", NodeKind::Field},      {"enumerator", NodeKind::Field},
    {"enumConstant", NodeKind::Field},  {"variable", NodeKind::Variable},
    {"var", NodeKind::Variable},        {"constant", NodeKind::Variable},
    {"const", NodeKind::Variable},      {"macro", NodeKind::Macro},
    {"define", NodeKind::Macro},        {"typedef", NodeKind::TypeAlias},
    {"type", NodeKind::TypeAlias},      {"alias", NodeKind::TypeAlias},
    {"typealias", NodeKind::TypeAlias}, {"talias", NodeKind::TypeAlias},
  }};

  /// Check whether a language belongs to the C family.
  ///
  /// \param language ctags language name.
  /// \return True for C, C++, CUDA, and Objective-C.
  bool
  is_c_family(std::string_view language)
  {
    return std::ranges::contains(kCFamily, language);
  }

  /// Check whether a node kind can enclose methods.
  ///
  /// \param kind Node kind.
  /// \return True for classes, structs, and unions.
  bool
  is_class_like(NodeKind kind)
  {
    return kind == NodeKind::Class || kind == NodeKind::Struct || kind == NodeKind::Union;
  }

  /// Split a ctags `inherits` field into base class names.
  ///
  /// Drops access specifiers and `virtual`, and template arguments, which may
  /// themselves contain commas.
  ///
  /// \param inherits Field value, e.g. "public Base<A, B>, virtual Other".
  /// \return Base names, e.g. {"Base", "Other"}.
  std::vector<std::string>
  split_bases(std::string_view inherits)
  {
    std::vector<std::string> bases;
    std::string current;
    int depth = 0;
    const auto flush = [&]
    {
      std::string_view name = current;
      for (const std::string_view word : {"public ", "protected ", "private ", "virtual "})
      {
        while (name.starts_with(word) || name.starts_with(' '))
        {
          name.remove_prefix(name.starts_with(' ') ? 1 : word.size());
        }
      }
      while (name.ends_with(' '))
      {
        name.remove_suffix(1);
      }
      if (!name.empty())
      {
        bases.emplace_back(name);
      }
      current.clear();
    };
    for (const char c : inherits)
    {
      if (c == '<')
      {
        ++depth;
      }
      else if (c == '>')
      {
        --depth;
      }
      else if (c == ',' && depth == 0)
      {
        flush();
      }
      else if (depth == 0)
      {
        current.push_back(c);
      }
    }
    flush();
    return bases;
  }

  /// Read a non-negative integer member of a ctags record.
  ///
  /// \param record Parsed JSON object.
  /// \param key Member name.
  /// \return The value, or 0 when absent or not a number.
  std::uint32_t
  uint_member(const Comprendo::Json &record, std::string_view key)
  {
    const Comprendo::Json *member = record.find(key);
    const double *number = member == nullptr ? nullptr : member->as_number();
    return number == nullptr || *number < 0 ? 0 : static_cast<std::uint32_t>(*number);
  }

  /// Count the lines of a text, counting a final unterminated line.
  ///
  /// \param text File contents.
  /// \return Number of lines.
  std::uint32_t
  count_lines(std::string_view text)
  {
    const auto newlines = static_cast<std::uint32_t>(std::ranges::count(text, '\n'));
    return newlines + (text.empty() || text.back() == '\n' ? 0U : 1U);
  }

  /// Read a whole file.
  ///
  /// \param path File to read.
  /// \return Contents, or nothing when the file cannot be opened.
  std::optional<std::string>
  read_file(const std::filesystem::path &path)
  {
    std::ifstream stream {path, std::ios::binary};
    if (!stream)
    {
      return std::nullopt;
    }
    return std::string {std::istreambuf_iterator<char> {stream}, {}};
  }

  /// Format a "path:line" evidence string.
  ///
  /// \param path File path.
  /// \param line 1-based line.
  /// \return Evidence text.
  std::string
  evidence(std::string_view path, std::uint32_t line)
  {
    return fmt::format("{}:{}", path, line);
  }

  /// Check whether a ctags search pattern shows a deleted function (`= delete`).
  ///
  /// \param pattern ctags pattern, e.g. `/^  T(const T &) = delete;$/`.
  /// \return True when an `=` is followed by `delete`.
  bool
  is_deleted_declaration(std::string_view pattern)
  {
    for (std::size_t eq = pattern.find('='); eq != std::string_view::npos;
         eq = pattern.find('=', eq + 1))
    {
      std::string_view rest = pattern.substr(eq + 1);
      rest.remove_prefix(std::min(rest.find_first_not_of(" \t"), rest.size()));
      if (rest.starts_with("delete"))
      {
        return true;
      }
    }
    return false;
  }

  /// A callable signature split into comparable parts.
  struct SplitSignature
  {
    /// Parameter types with names and default values removed.
    std::vector<std::string> parameters;
    /// Text after the parameter list, e.g. "const" or "const &&".
    std::string qualifiers;
  };

  /// Split text at top-level commas, ignoring commas nested in brackets.
  ///
  /// \param text Parameter list without the outer parentheses.
  /// \return Comma-separated pieces; empty for blank text.
  std::vector<std::string_view>
  split_top_level(std::string_view text)
  {
    std::vector<std::string_view> pieces;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
      const char c = text[i];
      depth += (c == '(' || c == '<' || c == '[' || c == '{') ? 1 : 0;
      depth -= (c == ')' || c == '>' || c == ']' || c == '}') ? 1 : 0;
      if (c == ',' && depth == 0)
      {
        pieces.push_back(text.substr(start, i - start));
        start = i + 1;
      }
    }
    if (text.find_first_not_of(" \t") != std::string_view::npos)
    {
      pieces.push_back(text.substr(start));
    }
    return pieces;
  }

  /// Reduce one parameter to its type: drop the default value and the name.
  ///
  /// The name is the last whitespace-separated token when it is an identifier
  /// and the token before it does not make it part of the type (as `unsigned`,
  /// `long`, or `const` would).
  ///
  /// \param parameter Parameter text as ctags prints it, e.g. "const T & value".
  /// \return Type text with single spaces, e.g. "const T &".
  std::string
  parameter_type(std::string_view parameter)
  {
    constexpr std::array<std::string_view, 11> kTypeWords = {
      "unsigned",
      "signed",
      "long",
      "short",
      "const",
      "volatile",
      "struct",
      "class",
      "enum",
      "union",
      "typename"
    };
    int depth = 0;
    for (std::size_t i = 0; i < parameter.size(); ++i)
    {
      const char c = parameter[i];
      depth += (c == '(' || c == '<' || c == '[') ? 1 : 0;
      depth -= (c == ')' || c == '>' || c == ']') ? 1 : 0;
      if (c == '=' && depth == 0)
      {
        parameter = parameter.substr(0, i);
        break;
      }
    }
    std::vector<std::string_view> tokens;
    for (const auto token : std::views::split(parameter, ' '))
    {
      if (!token.empty())
      {
        tokens.emplace_back(token.begin(), token.end());
      }
    }
    const auto is_identifier = [](std::string_view token)
    {
      return !token.empty()
        && (std::isalpha(static_cast<unsigned char>(token.front())) != 0 || token.front() == '_')
        && std::ranges::all_of(
          token, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
        );
    };
    if (
      tokens.size() >= 2 && is_identifier(tokens.back())
      && !std::ranges::contains(kTypeWords, tokens[tokens.size() - 2])
    )
    {
      tokens.pop_back();
    }
    std::string type;
    for (const std::string_view token : tokens)
    {
      type += type.empty() ? "" : " ";
      type += token;
    }
    return type;
  }

  /// Split a ctags signature such as "(int a,const T & b) const".
  ///
  /// \param signature ctags signature.
  /// \return Parameter types and qualifiers; `(void)` has no parameters.
  SplitSignature
  split_signature(std::string_view signature)
  {
    SplitSignature split;
    const std::size_t open = signature.find('(');
    if (open == std::string_view::npos)
    {
      split.qualifiers = std::string {signature};
      return split;
    }
    int depth = 0;
    std::size_t close = signature.size();
    for (std::size_t i = open; i < signature.size(); ++i)
    {
      depth += signature[i] == '(' ? 1 : 0;
      depth -= signature[i] == ')' ? 1 : 0;
      if (depth == 0)
      {
        close = i;
        break;
      }
    }
    for (const std::string_view parameter :
         split_top_level(signature.substr(open + 1, close - open - 1)))
    {
      split.parameters.push_back(parameter_type(parameter));
    }
    if (split.parameters.size() == 1 && split.parameters.front() == "void")
    {
      split.parameters.clear();
    }
    if (close < signature.size())
    {
      const std::string_view rest = signature.substr(close + 1);
      const std::size_t first = rest.find_first_not_of(" \t");
      split.qualifiers = first == std::string_view::npos ? "" : std::string {rest.substr(first)};
    }
    return split;
  }

  /// Find the definition a prototype declares.
  ///
  /// Tries, in order: an identical signature; identical parameter types and
  /// qualifiers; the only candidate with the same arity and qualifiers.
  ///
  /// \param chart Chart holding the candidate definitions.
  /// \param candidates Indices of callable definitions with the prototype's
  ///        qualified name.
  /// \param signature Prototype signature.
  /// \return Index of the matching definition, or nothing.
  std::optional<std::size_t>
  find_definition(
    const Comprendo::Chart &chart,
    const std::vector<std::size_t> &candidates,
    std::string_view signature
  )
  {
    const SplitSignature wanted = split_signature(signature);
    std::optional<std::size_t> parameters_match;
    std::optional<std::size_t> arity_match;
    std::size_t same_arity = 0;
    for (const std::size_t candidate : candidates)
    {
      const std::string &defined = chart.nodes[candidate].signature;
      if (defined == signature)
      {
        return candidate;
      }
      const SplitSignature split = split_signature(defined);
      if (split.qualifiers != wanted.qualifiers)
      {
        continue;
      }
      if (split.parameters == wanted.parameters && !parameters_match)
      {
        parameters_match = candidate;
      }
      if (split.parameters.size() == wanted.parameters.size())
      {
        ++same_arity;
        arity_match = candidate;
      }
    }
    if (parameters_match)
    {
      return parameters_match;
    }
    return same_arity == 1 ? arity_match : std::nullopt;
  }

  /// Find the class or struct a base name refers to.
  ///
  /// Looks the name up in the derived class's enclosing scopes, innermost
  /// first ("a::b::Base", "a::Base", "Base"), and accepts the first scope where
  /// exactly one class or struct has that qualified name.
  ///
  /// \param chart Chart holding the candidates.
  /// \param by_qualified_name Node indices per qualified name.
  /// \param scope Qualified scope of the derived class.
  /// \param base Base name as written.
  /// \return Node index, or nothing for unknown or ambiguous bases.
  std::optional<std::size_t>
  resolve_base(
    const Comprendo::Chart &chart,
    const std::unordered_map<std::string, std::vector<std::size_t>> &by_qualified_name,
    std::string_view scope,
    std::string_view base
  )
  {
    std::string_view prefix = scope;
    while (true)
    {
      const std::string name
        = prefix.empty() ? std::string {base} : fmt::format("{}::{}", prefix, base);
      if (const auto found = by_qualified_name.find(name); found != by_qualified_name.end())
      {
        std::optional<std::size_t> only;
        std::size_t matches = 0;
        for (const std::size_t index : found->second)
        {
          if (
            chart.nodes[index].kind == NodeKind::Class
            || chart.nodes[index].kind == NodeKind::Struct
          )
          {
            only = index;
            ++matches;
          }
        }
        if (matches == 1)
        {
          return only;
        }
      }
      if (prefix.empty())
      {
        return std::nullopt;
      }
      const std::size_t separator = prefix.rfind("::");
      prefix
        = separator == std::string_view::npos ? std::string_view {} : prefix.substr(0, separator);
    }
  }

  /// Check whether a node kind is callable.
  ///
  /// \param kind Node kind.
  /// \return True for functions and methods.
  bool
  is_callable(NodeKind kind)
  {
    return kind == NodeKind::Function || kind == NodeKind::Method;
  }

  /// A charted symbol awaiting its `Contains` parent.
  struct PendingSymbol
  {
    /// Index of the node in the chart.
    std::size_t node = 0;
    /// Enclosing scope's qualified name.
    std::string scope;
    /// Node kind the scope maps to, when known.
    std::optional<NodeKind> scope_kind;
  };

  /// Pick the `Contains` parent among nodes sharing a qualified name.
  ///
  /// Prefers a matching kind, then the same file; candidates are in canonical
  /// tag order, so ties resolve to the earliest definition.
  ///
  /// \param chart Chart under construction.
  /// \param candidates Node indices with the scope's qualified name.
  /// \param pending Symbol looking for a parent.
  /// \return Index of the parent node, or nothing.
  std::optional<std::size_t>
  choose_parent(
    const Comprendo::Chart &chart,
    const std::vector<std::size_t> &candidates,
    const PendingSymbol &pending
  )
  {
    const Comprendo::Node &child = chart.nodes[pending.node];
    std::optional<std::size_t> best;
    int best_score = -1;
    for (const std::size_t candidate : candidates)
    {
      if (candidate == pending.node)
      {
        continue;
      }
      const Comprendo::Node &node = chart.nodes[candidate];
      const int score
        = (pending.scope_kind == node.kind ? 2 : 0) + (node.file == child.file ? 1 : 0);
      if (score > best_score)
      {
        best = candidate;
        best_score = score;
      }
    }
    return best;
  }

  /// Run ctags over files in batches and collect each batch's standard output.
  ///
  /// Universal Ctags has no end-of-options marker, so callers must not pass paths
  /// starting with '-'.
  ///
  /// \param config Extraction settings (executable, batch size, root).
  /// \param options ctags options placed before the file names.
  /// \param files Repository-relative files.
  /// \param consume Callable receiving each batch's output; returns an error to stop.
  /// \return Nothing, or an error message.
  template <typename Consume>
  std::expected<void, std::string>
  run_ctags_batches(
    const Comprendo::ExtractConfig &config,
    const std::vector<std::string> &options,
    const std::vector<std::string> &files,
    Consume consume
  )
  {
    for (const auto batch : files | std::views::chunk(std::max<std::size_t>(config.ctags_batch, 1)))
    {
      std::vector<std::string> command = {config.ctags};
      command.insert(command.end(), options.begin(), options.end());
      command.insert(command.end(), batch.begin(), batch.end());
      const auto result = Comprendo::run_process(command, config.root);
      if (!result)
      {
        return std::unexpected(result.error());
      }
      if (result->exit_code != 0)
      {
        return std::unexpected(
          fmt::format("{} exited with code {}: {}", config.ctags, result->exit_code, result->err)
        );
      }
      if (auto consumed = consume(result->out); !consumed)
      {
        return consumed;
      }
    }
    return {};
  }

  /// Ask ctags for the language of each file and keep the configured languages.
  ///
  /// \param config Extraction settings.
  /// \param files Candidate files.
  /// \return Files with their language, or an error message.
  std::expected<std::vector<CtagsSourceFile>, std::string>
  detect_languages(const Comprendo::ExtractConfig &config, const std::vector<std::string> &files)
  {
    const ProfileScope profile {"ctags_languages"};
    const std::unordered_set<std::string> wanted(config.languages.begin(), config.languages.end());
    std::vector<std::string> safe_files;
    for (const std::string &file : files)
    {
      if (file.starts_with('-'))
      {
        Comprendo::Logger::warn("skipping {}: ctags would read it as an option", file);
        continue;
      }
      safe_files.push_back(file);
    }
    std::vector<CtagsSourceFile> sources;
    auto detected = run_ctags_batches(
      config,
      {"--print-language"},
      safe_files,
      [&](std::string_view output) -> std::expected<void, std::string>
      {
        for (const auto line_range : std::views::split(output, '\n'))
        {
          const std::string_view line {line_range.begin(), line_range.end()};
          const std::size_t colon = line.rfind(": ");
          if (colon == std::string_view::npos)
          {
            continue;
          }
          std::string language {line.substr(colon + 2)};
          if (wanted.contains(language))
          {
            sources.push_back({.path = std::string {line.substr(0, colon)}, .language = language});
          }
        }
        return {};
      }
    );
    if (!detected)
    {
      return std::unexpected(detected.error());
    }
    return sources;
  }

  /// Run ctags in JSON mode over the source files and parse every tag.
  ///
  /// \param config Extraction settings.
  /// \param sources Files to tag.
  /// \return Tags, or an error message.
  std::expected<std::vector<CtagsTag>, std::string>
  collect_tags(const Comprendo::ExtractConfig &config, const std::vector<CtagsSourceFile> &sources)
  {
    const ProfileScope profile {"ctags_tags"};
    std::vector<std::string> paths;
    paths.reserve(sources.size());
    for (const CtagsSourceFile &source : sources)
    {
      paths.push_back(source.path);
    }
    std::vector<CtagsTag> tags;
    auto collected = run_ctags_batches(
      config,
      {"--output-format=json",
       "--fields=+KnSesip",
       "--kinds-C=+p",
       "--kinds-C++=+p",
       "--kinds-CUDA=+p",
       "--sort=no",
       "-f",
       "-"},
      paths,
      [&tags](std::string_view output) -> std::expected<void, std::string>
      {
        for (const auto line_range : std::views::split(output, '\n'))
        {
          const std::string_view line {line_range.begin(), line_range.end()};
          if (line.empty())
          {
            continue;
          }
          auto tag = Comprendo::parse_ctags_line(line);
          if (!tag)
          {
            return std::unexpected(tag.error());
          }
          if (*tag)
          {
            tags.push_back(std::move(**tag));
          }
        }
        return {};
      }
    );
    if (!collected)
    {
      return std::unexpected(collected.error());
    }
    return tags;
  }

  /// Fill in line counts and include directives by reading each source file.
  ///
  /// \param root Project root.
  /// \param sources Files to read; updated in place.
  void
  read_sources(const std::filesystem::path &root, std::vector<CtagsSourceFile> &sources)
  {
    const ProfileScope profile {"read_sources"};
    for (CtagsSourceFile &source : sources)
    {
      const auto text = read_file(root / source.path);
      if (!text)
      {
        Comprendo::Logger::warn("cannot read {}", source.path);
        continue;
      }
      source.lines = count_lines(*text);
      if (is_c_family(source.language))
      {
        source.includes = Comprendo::scan_includes(*text);
      }
    }
  }

  /// Verify that the configured executable is Universal Ctags.
  ///
  /// \param ctags Executable name or path.
  /// \return Nothing, or an error message.
  std::expected<void, std::string>
  check_ctags(const std::string &ctags)
  {
    const auto version = Comprendo::run_process({ctags, "--version"});
    if (!version)
    {
      return std::unexpected(version.error());
    }
    if (version->exit_code != 0 || !version->out.contains("Universal Ctags"))
    {
      return std::unexpected(fmt::format("'{}' is not Universal Ctags", ctags));
    }
    return {};
  }

  /// Read the work tree's `git describe --always --dirty`.
  ///
  /// \param root Project root.
  /// \return The description, or empty when git is unavailable.
  std::string
  describe_commit(const std::filesystem::path &root)
  {
    const auto described = Comprendo::run_process({"git", "describe", "--always", "--dirty"}, root);
    if (!described || described->exit_code != 0)
    {
      return {};
    }
    std::string text = described->out;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    {
      text.pop_back();
    }
    return text;
  }
} // namespace

namespace Comprendo
{
  std::expected<std::optional<CtagsTag>, std::string>
  parse_ctags_line(std::string_view line)
  {
    auto record = Json::parse(line);
    if (!record)
    {
      return std::unexpected(fmt::format("bad ctags output '{}': {}", line, record.error()));
    }
    if (record->as_object() == nullptr)
    {
      return std::unexpected(fmt::format("bad ctags output '{}': not an object", line));
    }
    if (record->string_or_empty("_type") != "tag")
    {
      return std::optional<CtagsTag> {};
    }
    CtagsTag tag {
      .name = std::string {record->string_or_empty("name")},
      .path = std::string {record->string_or_empty("path")},
      .kind = std::string {record->string_or_empty("kind")},
      .scope = std::string {record->string_or_empty("scope")},
      .scope_kind = std::string {record->string_or_empty("scopeKind")},
      .signature = std::string {record->string_or_empty("signature")},
      .line = uint_member(*record, "line"),
      .end = uint_member(*record, "end"),
      .deleted = is_deleted_declaration(record->string_or_empty("pattern")),
      .bases = split_bases(record->string_or_empty("inherits")),
    };
    if (tag.name.empty() || tag.path.empty() || tag.kind.empty())
    {
      return std::unexpected(fmt::format("ctags record lacks name, path, or kind: '{}'", line));
    }
    return tag;
  }

  std::optional<NodeKind>
  map_ctags_kind(std::string_view language, std::string_view kind, std::string_view scope_kind)
  {
    if (std::ranges::contains(kSkippedKinds, kind))
    {
      return std::nullopt;
    }
    if (language == "Python" && kind == "member")
    {
      return NodeKind::Method;
    }
    const auto entry
      = std::ranges::find(kKindTable, kind, &std::pair<std::string_view, NodeKind>::first);
    const NodeKind mapped = entry == kKindTable.end() ? NodeKind::Other : entry->second;
    if (mapped == NodeKind::Function && !scope_kind.empty())
    {
      const auto scope = map_ctags_kind(language, scope_kind, {});
      if (scope && is_class_like(*scope))
      {
        return NodeKind::Method;
      }
    }
    return mapped;
  }

  Chart
  build_ctags_chart(
    const std::vector<CtagsSourceFile> &files,
    const std::vector<CtagsTag> &tags,
    const std::vector<std::string> &include_paths,
    std::vector<DeclarationSite> *declaration_sites
  )
  {
    const ProfileScope profile {"build_ctags_chart"};
    Chart chart;

    std::vector<const CtagsSourceFile *> sorted_files;
    sorted_files.reserve(files.size());
    for (const CtagsSourceFile &file : files)
    {
      sorted_files.push_back(&file);
    }
    std::ranges::sort(sorted_files, {}, &CtagsSourceFile::path);

    std::unordered_map<std::string, std::size_t> file_nodes;
    std::unordered_map<std::string, std::string_view> languages;
    for (const CtagsSourceFile *file : sorted_files)
    {
      if (file_nodes.contains(file->path))
      {
        continue;
      }
      const std::string name = std::filesystem::path {file->path}.filename().string();
      file_nodes.emplace(file->path, chart.nodes.size());
      languages.emplace(file->path, file->language);
      chart.nodes.push_back({
        .id = make_node_id(NodeKind::File, file->path, file->path, ""),
        .kind = NodeKind::File,
        .name = name,
        .qualified_name = file->path,
        .file = file->path,
        .line_start = 1,
        .line_end = file->lines,
        .loc = file->lines,
        .provenance = Provenance::Extractor,
      });
    }

    std::vector<const CtagsTag *> sorted_tags;
    sorted_tags.reserve(tags.size());
    for (const CtagsTag &tag : tags)
    {
      sorted_tags.push_back(&tag);
    }
    std::ranges::sort(
      sorted_tags,
      {},
      [](const CtagsTag *tag)
      {
        return std::tie(
          tag->path, tag->line, tag->end, tag->kind, tag->scope, tag->name, tag->signature
        );
      }
    );

    std::unordered_set<std::uint64_t> ids;
    for (const Node &node : chart.nodes)
    {
      ids.insert(node.id);
    }
    std::unordered_map<std::string, std::vector<std::size_t>> by_qualified_name;
    std::vector<std::pair<std::size_t, const CtagsTag *>> derived;
    std::vector<PendingSymbol> pending;
    for (const bool declarations : {false, true})
    {
      for (const CtagsTag *tag : sorted_tags)
      {
        const auto language = languages.find(tag->path);
        if (language == languages.end() || (tag->kind == "prototype") != declarations)
        {
          continue;
        }
        const auto kind = map_ctags_kind(language->second, tag->kind, tag->scope_kind);
        if (!kind || (is_callable(*kind) && tag->name.starts_with(kAnonymousPrefix)))
        {
          continue;
        }
        const std::string_view separator = is_c_family(language->second) ? "::" : ".";
        std::string qualified = tag->scope.empty()
          ? tag->name
          : fmt::format("{}{}{}", tag->scope, separator, tag->name);
        if (declarations)
        {
          std::vector<std::size_t> callables;
          if (const auto named = by_qualified_name.find(qualified); named != by_qualified_name.end())
          {
            std::ranges::copy_if(
              named->second,
              std::back_inserter(callables),
              [&chart](std::size_t index) { return is_callable(chart.nodes[index].kind); }
            );
          }
          if (tag->deleted)
          {
            continue;
          }
          if (const auto definition = find_definition(chart, callables, tag->signature))
          {
            if (declaration_sites != nullptr)
            {
              declaration_sites->push_back(
                {.node = chart.nodes[*definition].id, .path = tag->path, .line = tag->line}
              );
            }
            continue;
          }
        }
        const std::uint64_t id = make_node_id(*kind, qualified, tag->path, tag->signature);
        if (!ids.insert(id).second)
        {
          Logger::debug("dropping duplicate symbol {} at {}:{}", qualified, tag->path, tag->line);
          continue;
        }
        const std::uint32_t line_end = std::max(tag->end, tag->line);
        if (!tag->bases.empty() && is_class_like(*kind))
        {
          derived.emplace_back(chart.nodes.size(), tag);
        }
        const std::size_t index = chart.nodes.size();
        by_qualified_name[qualified].push_back(index);
        pending.push_back({
          .node = index,
          .scope = tag->scope,
          .scope_kind = tag->scope.empty() ? std::nullopt
                                           : map_ctags_kind(language->second, tag->scope_kind, {}),
        });
        chart.nodes.push_back({
          .id = id,
          .kind = *kind,
          .name = tag->name,
          .qualified_name = std::move(qualified),
          .file = tag->path,
          .line_start = tag->line,
          .line_end = line_end,
          .signature = tag->signature,
          .loc = tag->line == 0 ? 0 : line_end - tag->line + 1,
          .provenance = Provenance::Extractor,
        });
      }
    }

    for (const PendingSymbol &symbol : pending)
    {
      const Node &child = chart.nodes[symbol.node];
      std::optional<std::size_t> parent;
      if (!symbol.scope.empty())
      {
        if (
          const auto candidates = by_qualified_name.find(symbol.scope);
          candidates != by_qualified_name.end()
        )
        {
          parent = choose_parent(chart, candidates->second, symbol);
        }
      }
      const std::size_t parent_index = parent.value_or(file_nodes.at(child.file));
      chart.edges.push_back({
        .src = chart.nodes[parent_index].id,
        .dst = child.id,
        .kind = EdgeKind::Contains,
        .evidence = evidence(child.file, child.line_start),
        .provenance = Provenance::Extractor,
      });
    }

    std::unordered_set<std::string> known_files;
    for (const CtagsSourceFile *file : sorted_files)
    {
      known_files.insert(file->path);
    }
    std::set<std::pair<std::uint64_t, std::uint64_t>> include_edges;
    for (const CtagsSourceFile *file : sorted_files)
    {
      const std::uint64_t src = chart.nodes[file_nodes.at(file->path)].id;
      for (const IncludeDirective &directive : file->includes)
      {
        const auto target = resolve_include(file->path, directive, known_files, include_paths);
        if (!target || *target == file->path)
        {
          continue;
        }
        const std::uint64_t dst = chart.nodes[file_nodes.at(*target)].id;
        if (include_edges.emplace(src, dst).second)
        {
          chart.edges.push_back({
            .src = src,
            .dst = dst,
            .kind = EdgeKind::Includes,
            .evidence = evidence(file->path, directive.line),
            .provenance = Provenance::Extractor,
          });
        }
      }
    }

    for (const auto &[index, tag] : derived)
    {
      for (const std::string &base : tag->bases)
      {
        const auto target = resolve_base(chart, by_qualified_name, tag->scope, base);
        if (target && *target != index)
        {
          chart.edges.push_back({
            .src = chart.nodes[index].id,
            .dst = chart.nodes[*target].id,
            .kind = EdgeKind::Inherits,
            .evidence = evidence(tag->path, tag->line),
            .provenance = Provenance::Extractor,
          });
        }
      }
    }

    canonicalize(chart);
    return chart;
  }

  std::size_t
  apply_doc_comments(
    Chart &chart,
    const std::vector<DeclarationSite> &declarations,
    const std::unordered_map<std::string, std::vector<std::string>> &lines
  )
  {
    const ProfileScope profile {"apply_doc_comments"};
    std::unordered_map<std::uint64_t, std::vector<const DeclarationSite *>> sites;
    for (const DeclarationSite &site : declarations)
    {
      sites[site.node].push_back(&site);
    }
    const auto comment_at = [&lines](const std::string &path, std::uint32_t line)
    {
      const auto file = lines.find(path);
      return file == lines.end() ? std::nullopt : doc_comment_at(file->second, line);
    };
    std::size_t documented = 0;
    for (Node &node : chart.nodes)
    {
      if (!node.summary.empty())
      {
        continue;
      }
      std::optional<DocComment> doc;
      if (node.kind == NodeKind::File)
      {
        const auto file = lines.find(node.file);
        doc = file == lines.end() ? std::nullopt : file_doc_comment(file->second);
      }
      else
      {
        doc = comment_at(node.file, node.line_start);
        if (const auto found = sites.find(node.id); !doc && found != sites.end())
        {
          for (const DeclarationSite *site : found->second)
          {
            doc = doc ? doc : comment_at(site->path, site->line);
          }
        }
      }
      if (doc)
      {
        node.summary = std::move(doc->summary);
        node.detail = std::move(doc->detail);
        ++documented;
      }
    }
    return documented;
  }

  std::expected<Chart, std::string>
  CtagsExtractor::extract(const ExtractConfig &config)
  {
    const ProfileScope profile {"ctags_extract"};
    if (auto usable = check_ctags(config.ctags); !usable)
    {
      return std::unexpected(usable.error());
    }

    auto listing = enumerate_files(config.root, config.roots, config.excludes);
    if (!listing)
    {
      return std::unexpected(listing.error());
    }
    Logger::info(
      "{} files listed by {}",
      listing->files.size(),
      listing->source == FileSource::Git ? "git" : "directory walk"
    );

    auto sources = detect_languages(config, listing->files);
    if (!sources)
    {
      return std::unexpected(sources.error());
    }
    auto tags = collect_tags(config, *sources);
    if (!tags)
    {
      return std::unexpected(tags.error());
    }
    read_sources(config.root, *sources);

    std::vector<DeclarationSite> declarations;
    Chart chart = build_ctags_chart(*sources, *tags, config.include_paths, &declarations);
    std::unordered_map<std::string, std::vector<std::string>> lines;
    for (const CtagsSourceFile &source : *sources)
    {
      if (
        const auto text
        = is_c_family(source.language) ? read_file(config.root / source.path) : std::nullopt
      )
      {
        lines.emplace(source.path, split_source_lines(*text));
      }
    }
    const std::size_t documented = apply_doc_comments(chart, declarations, lines);
    const std::filesystem::path root = std::filesystem::weakly_canonical(config.root);
    chart.project = root.filename().string();
    chart.source_root = root.string();
    chart.git_commit = describe_commit(config.root);
    chart.generator = fmt::format("comprendo {} ({})", COMPRENDO_VERSION, name());
    chart.generated_at = fmt::format(
      "{:%Y-%m-%dT%H:%M:%SZ}",
      std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())
    );
    Logger::info(
      "ctags extraction: {} files, {} nodes ({} documented in code), {} edges",
      sources->size(),
      chart.nodes.size(),
      documented,
      chart.edges.size()
    );
    return chart;
  }
} // namespace Comprendo
