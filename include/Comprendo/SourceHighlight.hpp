#pragma once

/// \file
/// \brief C++ syntax highlighting for the viewer's Source panel: a line-by-line lexer.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Comprendo
{
  /// What a highlighted span of source text is.
  enum class TokenKind : std::uint8_t
  {
    /// Identifiers, operators, whitespace: drawn in the default colour.
    Text,
    /// A C++ keyword such as `class` or `return`.
    Keyword,
    /// A built-in type name such as `int` or `bool`.
    Type,
    /// A preprocessor directive line, e.g. `#include` or `#define`.
    Preprocessor,
    /// A string literal, including raw strings and include paths.
    String,
    /// A character literal.
    Character,
    /// A numeric literal.
    Number,
    /// A line or block comment.
    Comment,
  };

  /// A span of one line with its kind; offsets are bytes within the line.
  struct Token
  {
    /// First byte.
    std::uint32_t begin = 0;
    /// One past the last byte.
    std::uint32_t end = 0;
    /// Kind of the span.
    TokenKind kind = TokenKind::Text;

    /// Memberwise equality.
    ///
    /// \return True when all members are equal.
    bool
    operator==(const Token &) const = default;
  };

  /// Lexer state carried from one line to the next.
  struct LexState
  {
    /// Inside a `/* ... */` comment.
    bool in_block_comment = false;
    /// Inside a raw string literal with this delimiter (`R"delim( ... )delim"`).
    std::optional<std::string> raw_delimiter;
  };

  /// Split one line into highlighted spans.
  ///
  /// Spans cover the whole line in order; neighbouring spans of the same kind
  /// are merged. Block comments and raw strings continue into the next line
  /// through \p state.
  ///
  /// \param line Line without its terminator.
  /// \param state State after the previous line; updated for the next one.
  /// \return Spans of the line.
  [[nodiscard]]
  std::vector<Token>
  highlight_line(std::string_view line, LexState &state);

  /// A source file prepared for display.
  struct HighlightedSource
  {
    /// Lines with tabs expanded to spaces.
    std::vector<std::string> lines;
    /// Spans of each line.
    std::vector<std::vector<Token>> tokens;
  };

  /// Highlight a whole file.
  ///
  /// \param text File contents; `\n` or `\r\n` line ends.
  /// \param tab_width Columns per tab stop.
  /// \return Lines and their spans.
  [[nodiscard]]
  HighlightedSource
  highlight_source(std::string_view text, int tab_width = 4);

  /// Find a chart file on disk: the first of \p roots that contains it.
  ///
  /// \param file Repository-relative path from the chart.
  /// \param roots Directories to try in order (working directory, chart source root, ...).
  /// \return The existing file, or nothing.
  [[nodiscard]]
  std::optional<std::filesystem::path>
  resolve_source(std::string_view file, std::span<const std::filesystem::path> roots);
} // namespace Comprendo
