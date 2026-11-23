#pragma once

/// \file
/// \brief Doxygen comments in C-family sources, turned into short summaries and details.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Comprendo
{
  /// Longest summary taken from a comment, in bytes; longer ones are cut at a word.
  inline constexpr std::size_t kDocSummaryLimit = 200;

  /// Longest detail taken from a comment, in bytes; longer ones are cut at a sentence.
  inline constexpr std::size_t kDocDetailLimit = 600;

  /// Explanation taken from a documentation comment.
  struct DocComment
  {
    /// One sentence: `\brief`, or the first sentence of the description.
    std::string summary;
    /// The rest of the description, without parameter and return documentation.
    std::string detail;

    /// Memberwise equality.
    ///
    /// \return True when all members are equal.
    bool
    operator==(const DocComment &) const = default;
  };

  /// Turn the text of a documentation comment into a summary and a detail.
  ///
  /// Recognizes `\brief`/`@brief`, drops `\param`, `\tparam`, `\return`,
  /// `\throws`, `\see`, `\file`, and similar sections, skips `\\code` blocks,
  /// reduces inline markup (`\p x`, `\c x`, `\ref x`) to its word, joins
  /// paragraph lines, and caps both parts (kDocSummaryLimit, kDocDetailLimit).
  ///
  /// \param lines Comment lines with the comment markers already removed.
  /// \return The explanation, or nothing when the comment has no description.
  [[nodiscard]]
  std::optional<DocComment>
  parse_doc_text(std::span<const std::string> lines);

  /// Find the Doxygen comment documenting a declaration.
  ///
  /// Looks for a trailing `///<` or `//!<` comment on the declaration line,
  /// otherwise for a `///`/`//!` line block or a `/** */`/`/*! */` block ending
  /// directly above the declaration. Lines belonging to the declaration itself
  /// (attributes, template headers, a return type on its own line) may sit in
  /// between; a blank line or code ends the search. Plain `//` and `/* */`
  /// comments are not documentation and are ignored.
  ///
  /// \param lines Source file lines.
  /// \param line 1-based line of the declared name.
  /// \return The explanation, or nothing when the declaration is undocumented.
  [[nodiscard]]
  std::optional<DocComment>
  doc_comment_at(std::span<const std::string> lines, std::uint32_t line);

  /// Find a file's own documentation: the first Doxygen comment with `\file`.
  ///
  /// \param lines Source file lines.
  /// \return The explanation, or nothing when the file has none.
  [[nodiscard]]
  std::optional<DocComment>
  file_doc_comment(std::span<const std::string> lines);

  /// Split text into lines without terminators (a trailing `\r` is removed).
  ///
  /// \param text File contents.
  /// \return Lines.
  [[nodiscard]]
  std::vector<std::string>
  split_source_lines(std::string_view text);
} // namespace Comprendo
