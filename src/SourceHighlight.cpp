/// \file
/// \brief C++ syntax highlighting: a small line-by-line lexer for the Source panel.

#include "Comprendo/SourceHighlight.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
  using Comprendo::Token;
  using Comprendo::TokenKind;

  /// C++23 keywords (alternative operator spellings and contextual keywords included).
  constexpr auto kKeywords = std::to_array<std::string_view>({
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "auto",
    "bitand",
    "bitor",
    "break",
    "case",
    "catch",
    "class",
    "co_await",
    "co_return",
    "co_yield",
    "compl",
    "concept",
    "const",
    "const_cast",
    "consteval",
    "constexpr",
    "constinit",
    "continue",
    "decltype",
    "default",
    "delete",
    "do",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "final",
    "for",
    "friend",
    "goto",
    "if",
    "import",
    "inline",
    "module",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "override",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "using",
    "virtual",
    "volatile",
    "while",
    "xor",
    "xor_eq",
  });

  /// Built-in and fixed-width integer type names.
  constexpr auto kTypes = std::to_array<std::string_view>({
    "bool",     "char",    "char8_t",   "char16_t", "char32_t",  "wchar_t",  "double",
    "float",    "int",     "long",      "short",    "signed",    "unsigned", "void",
    "int8_t",   "int16_t", "int32_t",   "int64_t",  "uint8_t",   "uint16_t", "uint32_t",
    "uint64_t", "size_t",  "ptrdiff_t", "intptr_t", "uintptr_t",
  });

  /// Prefixes a string or character literal may carry.
  constexpr auto kLiteralPrefixes = std::to_array<std::string_view>({
    "u8",
    "u",
    "U",
    "L",
    "R",
    "u8R",
    "uR",
    "UR",
    "LR",
  });

  /// Collects spans for one line, merging neighbours of the same kind.
  class Spans
  {
  public:
    /// Add a span; empty spans are ignored.
    ///
    /// \param begin First byte.
    /// \param end One past the last byte.
    /// \param kind Kind of the span.
    void
    add(std::size_t begin, std::size_t end, TokenKind kind)
    {
      if (end <= begin)
      {
        return;
      }
      if (!m_tokens.empty() && m_tokens.back().kind == kind && m_tokens.back().end == begin)
      {
        m_tokens.back().end = static_cast<std::uint32_t>(end);
        return;
      }
      m_tokens.push_back(
        {.begin = static_cast<std::uint32_t>(begin),
         .end = static_cast<std::uint32_t>(end),
         .kind = kind}
      );
    }

    /// Hand out the spans.
    ///
    /// \return Spans in order.
    std::vector<Token>
    take()
    {
      return std::move(m_tokens);
    }

  private:
    /// Spans so far.
    std::vector<Token> m_tokens;
  };

  /// Check for an identifier character.
  ///
  /// \param c Character.
  /// \return True for letters, digits, and underscores.
  bool
  identifier_char(char c)
  {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
  }

  /// Check for a decimal digit.
  ///
  /// \param c Character.
  /// \return True for 0-9.
  bool
  digit(char c)
  {
    return c >= '0' && c <= '9';
  }

  /// Find the end of a quoted literal, honouring backslash escapes.
  ///
  /// \param line Line.
  /// \param open Position of the opening quote.
  /// \return One past the closing quote, or the line's end when unterminated.
  std::size_t
  quoted_end(std::string_view line, std::size_t open)
  {
    const char quote = line[open];
    for (std::size_t i = open + 1; i < line.size(); ++i)
    {
      if (line[i] == '\\')
      {
        ++i;
      }
      else if (line[i] == quote)
      {
        return i + 1;
      }
    }
    return line.size();
  }

  /// Find the end of a numeric literal: digits, letters (hex digits, suffixes),
  /// digit separators, the decimal point, and exponent signs.
  ///
  /// \param line Line.
  /// \param start First character of the number.
  /// \return One past the literal.
  std::size_t
  number_end(std::string_view line, std::size_t start)
  {
    std::size_t i = start;
    while (i < line.size())
    {
      const char c = line[i];
      const bool exponent_sign = (c == '+' || c == '-') && i > start
        && (line[i - 1] == 'e' || line[i - 1] == 'E' || line[i - 1] == 'p' || line[i - 1] == 'P');
      const bool separator = c == '\'' && i + 1 < line.size() && identifier_char(line[i + 1]);
      if (!identifier_char(c) && c != '.' && !exponent_sign && !separator)
      {
        break;
      }
      ++i;
    }
    return i;
  }

  /// Lex a string literal (plain or raw) whose opening quote is at \p quote.
  ///
  /// \param line Line.
  /// \param quote Position of the opening quote.
  /// \param raw True for a raw string (`R"delim(...)delim"`).
  /// \param state Receives the raw delimiter when the string continues.
  /// \return One past the literal, or the line's end.
  std::size_t
  string_end(std::string_view line, std::size_t quote, bool raw, Comprendo::LexState &state)
  {
    if (!raw)
    {
      return quoted_end(line, quote);
    }
    const std::size_t paren = line.find('(', quote + 1);
    if (paren == std::string_view::npos)
    {
      return line.size();
    }
    const std::string delimiter {line.substr(quote + 1, paren - quote - 1)};
    const std::string terminator = ")" + delimiter + "\"";
    const std::size_t close = line.find(terminator, paren + 1);
    if (close == std::string_view::npos)
    {
      state.raw_delimiter = delimiter;
      return line.size();
    }
    return close + terminator.size();
  }

  /// Kind of an identifier.
  ///
  /// \param word Identifier.
  /// \return Keyword, Type, or Text.
  TokenKind
  word_kind(std::string_view word)
  {
    if (std::ranges::find(kKeywords, word) != kKeywords.end())
    {
      return TokenKind::Keyword;
    }
    if (std::ranges::find(kTypes, word) != kTypes.end())
    {
      return TokenKind::Type;
    }
    return TokenKind::Text;
  }

  /// Lex a preprocessor directive name and, for includes, the header name.
  ///
  /// \param line Line.
  /// \param hash Position of `#`.
  /// \param spans Receives the spans.
  /// \return Position after what was lexed.
  std::size_t
  directive(std::string_view line, std::size_t hash, Spans &spans)
  {
    std::size_t i = hash + 1;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
    {
      ++i;
    }
    const std::size_t name_start = i;
    while (i < line.size() && identifier_char(line[i]))
    {
      ++i;
    }
    spans.add(hash, i, TokenKind::Preprocessor);
    const std::string_view name = line.substr(name_start, i - name_start);
    if (name != "include" && name != "import" && name != "include_next")
    {
      return i;
    }
    std::size_t open = i;
    while (open < line.size() && (line[open] == ' ' || line[open] == '\t'))
    {
      ++open;
    }
    if (open < line.size() && line[open] == '<')
    {
      const std::size_t close = line.find('>', open + 1);
      const std::size_t end = close == std::string_view::npos ? line.size() : close + 1;
      spans.add(i, open, TokenKind::Text);
      spans.add(open, end, TokenKind::String);
      return end;
    }
    return i;
  }
} // namespace

namespace Comprendo
{
  std::vector<Token>
  highlight_line(std::string_view line, LexState &state)
  {
    Spans spans;
    std::size_t pos = 0;
    if (state.in_block_comment)
    {
      const std::size_t close = line.find("*/");
      pos = close == std::string_view::npos ? line.size() : close + 2;
      state.in_block_comment = close == std::string_view::npos;
      spans.add(0, pos, TokenKind::Comment);
    }
    else if (state.raw_delimiter)
    {
      const std::string terminator = ")" + *state.raw_delimiter + "\"";
      const std::size_t close = line.find(terminator);
      pos = close == std::string_view::npos ? line.size() : close + terminator.size();
      if (close != std::string_view::npos)
      {
        state.raw_delimiter.reset();
      }
      spans.add(0, pos, TokenKind::String);
    }
    else
    {
      const std::size_t first = line.find_first_not_of(" \t");
      if (first != std::string_view::npos && line[first] == '#')
      {
        spans.add(0, first, TokenKind::Text);
        pos = directive(line, first, spans);
      }
    }

    while (pos < line.size())
    {
      const char c = line[pos];
      const std::string_view rest = line.substr(pos);
      if (rest.starts_with("//"))
      {
        spans.add(pos, line.size(), TokenKind::Comment);
        break;
      }
      if (rest.starts_with("/*"))
      {
        const std::size_t close = line.find("*/", pos + 2);
        const std::size_t end = close == std::string_view::npos ? line.size() : close + 2;
        state.in_block_comment = close == std::string_view::npos;
        spans.add(pos, end, TokenKind::Comment);
        pos = end;
        continue;
      }
      if (c == '"' || c == '\'')
      {
        const std::size_t end
          = c == '"' ? string_end(line, pos, false, state) : quoted_end(line, pos);
        spans.add(pos, end, c == '"' ? TokenKind::String : TokenKind::Character);
        pos = end;
        continue;
      }
      if (digit(c) || (c == '.' && pos + 1 < line.size() && digit(line[pos + 1])))
      {
        const std::size_t end = number_end(line, pos);
        spans.add(pos, end, TokenKind::Number);
        pos = end;
        continue;
      }
      if (identifier_char(c))
      {
        std::size_t end = pos;
        while (end < line.size() && identifier_char(line[end]))
        {
          ++end;
        }
        const std::string_view word = line.substr(pos, end - pos);
        const bool quote_follows = end < line.size() && (line[end] == '"' || line[end] == '\'');
        if (quote_follows && std::ranges::find(kLiteralPrefixes, word) != kLiteralPrefixes.end())
        {
          const bool is_string = line[end] == '"';
          const std::size_t literal_end
            = is_string ? string_end(line, end, word.ends_with('R'), state) : quoted_end(line, end);
          spans.add(pos, literal_end, is_string ? TokenKind::String : TokenKind::Character);
          pos = literal_end;
          continue;
        }
        spans.add(pos, end, word_kind(word));
        pos = end;
        continue;
      }
      spans.add(pos, pos + 1, TokenKind::Text);
      ++pos;
    }
    return spans.take();
  }

  HighlightedSource
  highlight_source(std::string_view text, int tab_width)
  {
    HighlightedSource source;
    LexState state;
    const auto width = static_cast<std::size_t>(std::max(tab_width, 1));
    std::size_t start = 0;
    while (start <= text.size())
    {
      const std::size_t newline = text.find('\n', start);
      if (newline == std::string_view::npos && start == text.size() && !source.lines.empty())
      {
        break;
      }
      std::string_view raw = text.substr(
        start, newline == std::string_view::npos ? std::string_view::npos : newline - start
      );
      if (raw.ends_with('\r'))
      {
        raw.remove_suffix(1);
      }
      std::string line;
      for (const char c : raw)
      {
        if (c == '\t')
        {
          line.append(width - (line.size() % width), ' ');
        }
        else
        {
          line.push_back(c);
        }
      }
      source.tokens.push_back(highlight_line(line, state));
      source.lines.push_back(std::move(line));
      if (newline == std::string_view::npos)
      {
        break;
      }
      start = newline + 1;
    }
    return source;
  }

  std::optional<std::filesystem::path>
  resolve_source(std::string_view file, std::span<const std::filesystem::path> roots)
  {
    if (file.empty())
    {
      return std::nullopt;
    }
    for (const std::filesystem::path &root : roots)
    {
      if (root.empty())
      {
        continue;
      }
      std::filesystem::path candidate = (root / file).lexically_normal();
      std::error_code error;
      if (std::filesystem::is_regular_file(candidate, error))
      {
        return candidate;
      }
    }
    return std::nullopt;
  }
} // namespace Comprendo
