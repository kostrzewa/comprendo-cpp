/// \file
/// \brief Doxygen comments in C-family sources, turned into short summaries and details.

#include "Comprendo/DocComments.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
  using Comprendo::DocComment;

  /// Lines between a comment and the declared name that may belong to the
  /// declaration (attributes, template header, return type).
  constexpr std::size_t kMaxDeclarationLines = 6;

  /// Longest `/* */` block searched upwards for its opening line.
  constexpr std::size_t kMaxBlockLines = 200;

  /// Lines at the top of a file searched for its `\file` comment.
  constexpr std::size_t kFileCommentLines = 80;

  /// Commands whose section is dropped (parameters, returns, metadata).
  constexpr std::array<std::string_view, 27> kSkippedCommands = {
    "param",   "tparam",    "return",     "returns", "retval", "result",   "throw",
    "throws",  "exception", "see",        "sa",      "since",  "author",   "authors",
    "date",    "version",   "copyright",  "file",    "class",  "struct",   "fn",
    "ingroup", "defgroup",  "addtogroup", "name",    "todo",   "internal",
  };

  /// Commands whose text stays part of the description.
  constexpr std::array<std::string_view, 9> kKeptCommands = {
    "details",
    "note",
    "warning",
    "attention",
    "remark",
    "remarks",
    "pre",
    "post",
    "invariant",
  };

  /// Commands opening verbatim blocks, skipped up to their `end` command.
  constexpr std::array<std::string_view, 4> kVerbatimCommands = {"code", "verbatim", "dot", "msc"};

  /// Inline commands reduced to the word that follows them.
  constexpr std::array<std::string_view, 9> kInlineCommands = {
    "p",
    "a",
    "c",
    "e",
    "em",
    "b",
    "ref",
    "link",
    "copydoc",
  };

  /// Remove leading and trailing whitespace.
  ///
  /// \param text Text.
  /// \return Trimmed view.
  std::string_view
  trim(std::string_view text)
  {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
      return {};
    }
    const std::size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
  }

  /// Split a leading `\cmd` or `@cmd` off a comment line.
  ///
  /// \param line Trimmed comment line.
  /// \return The command name and the rest of the line, or nothing for text lines.
  std::optional<std::pair<std::string_view, std::string_view>>
  command_of(std::string_view line)
  {
    if (line.size() < 2 || (line.front() != '\\' && line.front() != '@'))
    {
      return std::nullopt;
    }
    std::size_t end = 1;
    while (end < line.size() && std::isalpha(static_cast<unsigned char>(line[end])) != 0)
    {
      ++end;
    }
    if (end == 1)
    {
      return std::nullopt;
    }
    return std::pair {line.substr(1, end - 1), trim(line.substr(end))};
  }

  /// Reduce inline markup (`\p name`, `@c code`, `\ref target`) to its words.
  ///
  /// \param text Comment text.
  /// \return Plain text.
  std::string
  strip_inline(std::string_view text)
  {
    std::string result;
    std::size_t i = 0;
    while (i < text.size())
    {
      const bool at_word_start = i == 0 || text[i - 1] == ' ' || text[i - 1] == '(';
      if (at_word_start && (text[i] == '\\' || text[i] == '@'))
      {
        std::size_t end = i + 1;
        while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end])) != 0)
        {
          ++end;
        }
        const std::string_view command = text.substr(i + 1, end - i - 1);
        if (command == "endlink")
        {
          i = end;
          continue;
        }
        if (std::ranges::contains(kInlineCommands, command) && end < text.size() && text[end] == ' ')
        {
          i = end + 1;
          continue;
        }
      }
      result.push_back(text[i]);
      ++i;
    }
    return result;
  }

  /// Length of the first sentence of a paragraph.
  ///
  /// A sentence ends at `.`, `!`, or `?` followed by a space, except after
  /// common abbreviations (e.g., i.e., etc., vs.).
  ///
  /// \param paragraph Paragraph text.
  /// \return Length including the end mark, or the whole length.
  std::size_t
  first_sentence_length(std::string_view paragraph)
  {
    constexpr std::array<std::string_view, 4> kAbbreviations = {"e.g", "i.e", "etc", "vs"};
    for (std::size_t i = 0; i + 1 < paragraph.size(); ++i)
    {
      const char c = paragraph[i];
      if ((c != '.' && c != '!' && c != '?') || paragraph[i + 1] != ' ')
      {
        continue;
      }
      const std::string_view before = paragraph.substr(0, i);
      const bool abbreviation = std::ranges::any_of(
        kAbbreviations, [&](std::string_view word) { return before.ends_with(word); }
      );
      if (!abbreviation)
      {
        return i + 1;
      }
    }
    return paragraph.size();
  }

  /// Shorten text to a limit, preferring a sentence end, then a word end.
  ///
  /// \param text Text to shorten.
  /// \param limit Largest length in bytes, before the ellipsis.
  /// \param sentences True to cut at a sentence end when one is available.
  /// \return The text, cut and followed by "…" when it was too long.
  std::string
  cap(std::string text, std::size_t limit, bool sentences)
  {
    if (text.size() <= limit)
    {
      return text;
    }
    std::size_t cut = std::string::npos;
    if (sentences)
    {
      const std::size_t end = text.rfind(". ", limit);
      cut = end == std::string::npos ? end : end + 1;
    }
    if (cut == std::string::npos || cut < limit / 2)
    {
      cut = text.rfind(' ', limit);
    }
    if (cut == std::string::npos)
    {
      cut = limit;
    }
    text.resize(cut);
    return text + "…";
  }

  /// Check whether a trimmed line is a `///` or `//!` documentation line.
  ///
  /// \param line Trimmed line.
  /// \return True for line documentation, false for `///<` trailing markers.
  bool
  is_doc_line(std::string_view line)
  {
    return (line.starts_with("///") || line.starts_with("//!")) && !line.starts_with("///<")
      && !line.starts_with("//!<");
  }

  /// Strip the `///` or `//!` marker and one following space.
  ///
  /// \param line Trimmed documentation line.
  /// \return Comment text; empty for separator lines such as `//////`.
  std::string
  strip_doc_line(std::string_view line)
  {
    std::string_view text = line.substr(3);
    if (text.find_first_not_of('/') == std::string_view::npos)
    {
      return {};
    }
    if (text.starts_with(' '))
    {
      text.remove_prefix(1);
    }
    return std::string {text};
  }

  /// Strip the markers of a `/** ... */` block line.
  ///
  /// \param line Trimmed line of the block.
  /// \param first True for the opening line.
  /// \return Comment text.
  std::string
  strip_block_line(std::string_view line, bool first)
  {
    if (first)
    {
      const std::size_t open = line.find("/*");
      line.remove_prefix(open + 3);
    }
    if (const std::size_t close = line.rfind("*/"); close != std::string_view::npos)
    {
      line = line.substr(0, close);
    }
    line = trim(line);
    if (!first && line.starts_with('*'))
    {
      line.remove_prefix(1);
    }
    return std::string {trim(line)};
  }

  /// Check whether a line opens a Doxygen block (`/**` or `/*!`, not `/**/`).
  ///
  /// \param line Line.
  /// \return True for a documentation block opening.
  bool
  opens_doc_block(std::string_view line)
  {
    const std::size_t open = line.find("/*");
    if (open == std::string_view::npos || open + 2 >= line.size())
    {
      return false;
    }
    const char marker = line[open + 2];
    return (marker == '*' || marker == '!') && !line.substr(open).starts_with("/**/");
  }

  /// Collect the block comment that ends on a line, walking upwards.
  ///
  /// \param lines Source lines.
  /// \param last Index of the line ending with `*/`.
  /// \return Comment text lines, or nothing when the block is not documentation.
  std::optional<std::vector<std::string>>
  block_ending_at(std::span<const std::string> lines, std::size_t last)
  {
    for (std::size_t steps = 0; steps < kMaxBlockLines; ++steps)
    {
      const std::size_t first = last - steps;
      if (lines[first].find("/*") != std::string::npos)
      {
        if (!opens_doc_block(lines[first]))
        {
          return std::nullopt;
        }
        std::vector<std::string> text;
        for (std::size_t i = first; i <= last; ++i)
        {
          text.push_back(strip_block_line(trim(lines[i]), i == first));
        }
        return text;
      }
      if (first == 0)
      {
        break;
      }
    }
    return std::nullopt;
  }

  /// Check whether a line can be part of a declaration above its name line.
  ///
  /// \param line Trimmed, non-empty, non-comment line.
  /// \return False for code that ends a statement or block, and preprocessor lines.
  bool
  continues_declaration(std::string_view line)
  {
    return !line.starts_with('#') && !line.ends_with(';') && !line.ends_with('{')
      && !line.ends_with('}') && !line.ends_with(':');
  }
} // namespace

namespace Comprendo
{
  std::optional<DocComment>
  parse_doc_text(std::span<const std::string> lines)
  {
    enum class Section : std::uint8_t
    {
      Description,
      Brief,
      Skipped,
      Verbatim,
    };
    Section section = Section::Description;
    std::string verbatim_end;
    std::string brief;
    std::vector<std::string> paragraphs;
    std::string current;
    const auto flush = [&]
    {
      if (!current.empty())
      {
        paragraphs.push_back(std::move(current));
        current.clear();
      }
    };
    const auto append = [](std::string &to, std::string_view text)
    {
      if (!text.empty())
      {
        to += to.empty() ? "" : " ";
        to += text;
      }
    };

    for (const std::string &raw : lines)
    {
      std::string_view line = trim(raw);
      if (section == Section::Verbatim)
      {
        const auto command = command_of(line);
        section = command && command->first == verbatim_end ? Section::Description : section;
        continue;
      }
      if (line.empty())
      {
        flush();
        section = Section::Description;
        continue;
      }
      if (const auto command = command_of(line))
      {
        const auto [name, rest] = *command;
        if (std::ranges::contains(kVerbatimCommands, name))
        {
          flush();
          section = Section::Verbatim;
          verbatim_end = "end" + std::string {name};
          continue;
        }
        if (name == "brief" || name == "short")
        {
          flush();
          section = Section::Brief;
          line = rest;
        }
        else if (std::ranges::contains(kKeptCommands, name) || name == "deprecated")
        {
          flush();
          section = Section::Description;
          line = rest;
        }
        else if (std::ranges::contains(kSkippedCommands, name))
        {
          flush();
          section = Section::Skipped;
          continue;
        }
      }
      if (section == Section::Skipped)
      {
        continue;
      }
      append(section == Section::Brief ? brief : current, strip_inline(line));
    }
    flush();

    DocComment doc;
    std::size_t first_paragraph = 0;
    if (!brief.empty())
    {
      doc.summary = brief;
    }
    else if (!paragraphs.empty())
    {
      const std::size_t length = first_sentence_length(paragraphs.front());
      doc.summary = paragraphs.front().substr(0, length);
      std::string rest {trim(std::string_view {paragraphs.front()}.substr(length))};
      if (rest.empty())
      {
        first_paragraph = 1;
      }
      else
      {
        paragraphs.front() = std::move(rest);
      }
    }
    if (doc.summary.empty())
    {
      return std::nullopt;
    }
    for (std::size_t i = first_paragraph; i < paragraphs.size(); ++i)
    {
      doc.detail += doc.detail.empty() ? "" : "\n\n";
      doc.detail += paragraphs[i];
    }
    doc.summary = cap(std::move(doc.summary), kDocSummaryLimit, false);
    doc.detail = cap(std::move(doc.detail), kDocDetailLimit, true);
    return doc;
  }

  std::optional<DocComment>
  doc_comment_at(std::span<const std::string> lines, std::uint32_t line)
  {
    if (line == 0 || line > lines.size())
    {
      return std::nullopt;
    }
    const std::string &declaration = lines[line - 1];
    for (const std::string_view marker : {"///<", "//!<", "/**<", "/*!<"})
    {
      if (const std::size_t at = declaration.find(marker); at != std::string::npos)
      {
        std::string_view text = std::string_view {declaration}.substr(at + marker.size());
        if (const std::size_t close = text.find("*/"); close != std::string_view::npos)
        {
          text = text.substr(0, close);
        }
        const std::array trailing = {std::string {trim(text)}};
        return parse_doc_text(trailing);
      }
    }

    std::size_t index = line - 1;
    for (std::size_t skipped = 0;; ++skipped)
    {
      if (index == 0)
      {
        return std::nullopt;
      }
      --index;
      const std::string_view text = trim(lines[index]);
      if (text.empty())
      {
        return std::nullopt;
      }
      if (is_doc_line(text) || text.ends_with("*/") || text.starts_with("//"))
      {
        break;
      }
      if (!continues_declaration(text) || skipped == kMaxDeclarationLines)
      {
        return std::nullopt;
      }
    }

    const std::string_view last = trim(lines[index]);
    if (is_doc_line(last))
    {
      std::size_t first = index;
      while (first > 0 && is_doc_line(trim(lines[first - 1])))
      {
        --first;
      }
      std::vector<std::string> text;
      for (std::size_t i = first; i <= index; ++i)
      {
        text.push_back(strip_doc_line(trim(lines[i])));
      }
      return parse_doc_text(text);
    }
    if (last.ends_with("*/"))
    {
      const auto text = block_ending_at(lines, index);
      return text ? parse_doc_text(*text) : std::nullopt;
    }
    return std::nullopt;
  }

  std::optional<DocComment>
  file_doc_comment(std::span<const std::string> lines)
  {
    const std::size_t end = std::min(lines.size(), kFileCommentLines);
    std::size_t index = 0;
    while (index < end)
    {
      const std::string_view line = trim(lines[index]);
      std::vector<std::string> text;
      if (is_doc_line(line))
      {
        while (index < lines.size() && is_doc_line(trim(lines[index])))
        {
          text.push_back(strip_doc_line(trim(lines[index])));
          ++index;
        }
      }
      else if (opens_doc_block(line))
      {
        const std::size_t first = index;
        while (index < lines.size()
               && (index == first || lines[index - 1].find("*/") == std::string::npos))
        {
          text.push_back(strip_block_line(trim(lines[index]), index == first));
          ++index;
        }
      }
      else
      {
        ++index;
        continue;
      }
      const bool has_file = std::ranges::any_of(
        text,
        [](const std::string &part) { return part.contains("\\file") || part.contains("@file"); }
      );
      if (has_file)
      {
        return parse_doc_text(text);
      }
    }
    return std::nullopt;
  }

  std::vector<std::string>
  split_source_lines(std::string_view text)
  {
    std::vector<std::string> lines;
    for (const auto line : std::views::split(text, '\n'))
    {
      std::string_view view {line.begin(), line.end()};
      if (view.ends_with('\r'))
      {
        view.remove_suffix(1);
      }
      lines.emplace_back(view);
    }
    return lines;
  }
} // namespace Comprendo
