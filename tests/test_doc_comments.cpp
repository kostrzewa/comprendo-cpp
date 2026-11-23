/// \file
/// \brief Tests for turning Doxygen comments into node summaries and details.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Comprendo/Chart.hpp"
#include "Comprendo/CtagsExtractor.hpp"
#include "Comprendo/DocComments.hpp"
#include "Comprendo/ExtractConfig.hpp"
#include "Comprendo/Process.hpp"
#include "TempDir.hpp"

using Comprendo::DocComment;

namespace
{
  /// Parse comment text given as one string with newlines.
  ///
  /// \param text Comment lines without markers.
  /// \return Parsed comment.
  std::optional<DocComment>
  parse(const std::string &text)
  {
    return Comprendo::parse_doc_text(Comprendo::split_source_lines(text));
  }

  /// Find the comment documenting a line of a source given as one string.
  ///
  /// \param source Source text.
  /// \param line 1-based declaration line.
  /// \return Parsed comment.
  std::optional<DocComment>
  at(const std::string &source, std::uint32_t line)
  {
    return Comprendo::doc_comment_at(Comprendo::split_source_lines(source), line);
  }
} // namespace

/// Summaries must say what the author wrote first, briefly; parameter and return
/// sections repeat the signature and would bloat the Inspector. Parses comments
/// with and without `\brief`, with parameter sections, inline markup, a code
/// block, and an abbreviation in the first sentence, and checks summary and
/// detail of each.
TEST_CASE("doc text yields a brief summary and a lean detail", "[doc]")
{
  CHECK(
    parse("\\brief Area of a circle.\n\nUses \\p pi exactly.\n\\param c Circle.\n\\return Area.")
    == DocComment {.summary = "Area of a circle.", .detail = "Uses pi exactly."}
  );
  CHECK(
    parse("Finds the node. Prefers the smallest one,\ne.g. a method over its class.")
    == DocComment {
      .summary = "Finds the node.",
      .detail = "Prefers the smallest one, e.g. a method over its class."
    }
  );
  CHECK(
    parse(
      "@brief Parses input, e.g. JSON.\n\\code\nparse(\"x\");\n\\endcode\n\\note Not thread-safe."
    )
    == DocComment {.summary = "Parses input, e.g. JSON.", .detail = "Not thread-safe."}
  );
  CHECK_FALSE(parse("\\param x Only parameters.\n\\return Nothing else.").has_value());
  CHECK_FALSE(parse("").has_value());
}

/// Long comments must not turn into long summaries; the caps keep the chart's
/// explanations short. Parses a single overlong sentence and a long multi-
/// sentence description and checks both parts stay within their limits and
/// end with an ellipsis at a word or sentence boundary.
TEST_CASE("doc text is capped", "[doc]")
{
  std::string words;
  for (int i = 0; i < 60; ++i)
  {
    words += "word ";
  }
  std::string sentences;
  for (int i = 0; i < 40; ++i)
  {
    sentences += "This sentence adds detail. ";
  }
  const auto doc = parse("\\brief " + words + "\n\n" + sentences);
  CHECK(doc.has_value());
  if (!doc)
  {
    return;
  }
  CHECK(doc->summary.size() <= Comprendo::kDocSummaryLimit + std::string {"…"}.size());
  CHECK(doc->summary.ends_with("word…"));
  CHECK(doc->detail.size() <= Comprendo::kDocDetailLimit + std::string {"…"}.size());
  CHECK(doc->detail.ends_with("detail.…"));
}

/// Doxygen comments sit above the declared name with attributes or a return
/// type in between, or trail a member; plain comments and comments separated
/// by a blank line document nothing. Locates the comment for declarations in
/// each of these layouts and checks which are found.
TEST_CASE("doc comments are found next to declarations", "[doc]")
{
  const std::string source = "/// \\brief Area.\n"         // 1
                             "[[nodiscard]]\n"             // 2
                             "double\n"                    // 3
                             "area(const Circle &c);\n"    // 4
                             "/** Block doc.\n"            // 5
                             " *  More text. */\n"         // 6
                             "struct S {\n"                // 7
                             "  int x; ///< Member doc.\n" // 8
                             "  int y; //!< Other doc.\n"  // 9
                             "};\n"                        // 10
                             "// Plain comment.\n"         // 11
                             "void plain();\n"             // 12
                             "/// Detached.\n"             // 13
                             "\n"                          // 14
                             "void detached();\n"          // 15
                             "/* Plain block. */\n"        // 16
                             "void block();\n"             // 17
                             "int z;\n"                    // 18
                             "void after_code();\n";       // 19
  CHECK(at(source, 4) == DocComment {.summary = "Area."});
  CHECK(at(source, 7) == DocComment {.summary = "Block doc.", .detail = "More text."});
  CHECK(at(source, 8) == DocComment {.summary = "Member doc."});
  CHECK(at(source, 9) == DocComment {.summary = "Other doc."});
  CHECK_FALSE(at(source, 12).has_value());
  CHECK_FALSE(at(source, 15).has_value());
  CHECK_FALSE(at(source, 17).has_value());
  CHECK_FALSE(at(source, 19).has_value());
  CHECK_FALSE(at(source, 0).has_value());
  CHECK_FALSE(at(source, 99).has_value());
}

/// A file node is explained by the file's own `\file` comment, not by the
/// first comment in it. Checks a file with a `\file` block after a license
/// comment and a file without one.
TEST_CASE("file comments come from \\file blocks", "[doc]")
{
  const auto lines = Comprendo::split_source_lines(
    "// SPDX-License-Identifier: MIT\n/// \\file\n/// \\brief Area formulas.\n\n#pragma once\n"
  );
  CHECK(Comprendo::file_doc_comment(lines) == DocComment {.summary = "Area formulas."});
  CHECK_FALSE(
    Comprendo::file_doc_comment(Comprendo::split_source_lines("/// Not a file doc.\nint x;\n"))
      .has_value()
  );
}

/// Most projects document functions at their header declaration and define
/// them in a source file, where the chart places the node; the documentation
/// must follow. Extracts the doc_comments fixture with ctags and checks the
/// definitions of area (documented in the header), square (detached comment),
/// and perimeter (plain comment), the struct, its documented and undocumented
/// fields, and the header's file comment.
TEST_CASE("ctags extraction takes summaries from doc comments", "[doc][fixture]")
{
  const auto version = Comprendo::run_process({"ctags", "--version"});
  if (!version || version->exit_code != 0)
  {
    SKIP("Universal Ctags is not installed");
  }
  const Comprendo::Testing::TempDir dir {"doc-comments"};
  std::filesystem::copy(
    COMPRENDO_FIXTURE_DIR "/doc_comments", dir.path(), std::filesystem::copy_options::recursive
  );
  auto config = Comprendo::load_extract_config(dir.path());
  CHECK(config.has_value());
  if (!config)
  {
    return;
  }
  const auto chart = Comprendo::CtagsExtractor {}.extract(*config);
  CHECK(chart.has_value());
  if (!chart)
  {
    return;
  }
  const auto summary_of = [&](const std::string &qualified_name, const std::string &file)
  {
    const auto node = std::ranges::find_if(
      chart->nodes,
      [&](const Comprendo::Node &n) { return n.qualified_name == qualified_name && n.file == file; }
    );
    return node == chart->nodes.end() ? std::optional<std::string> {}
                                      : std::optional {node->summary};
  };
  const auto area = std::ranges::find_if(
    chart->nodes, [](const Comprendo::Node &n) { return n.qualified_name == "shapes::area"; }
  );
  CHECK(area != chart->nodes.end());
  if (area != chart->nodes.end())
  {
    CHECK(area->file == "src/area.cpp");
    CHECK(area->summary == "Area of a circle.");
    CHECK(area->detail == "Uses the exact value of pi, so results match the C library.");
    CHECK(area->provenance == Comprendo::Provenance::Extractor);
  }
  CHECK(summary_of("shapes::square", "src/area.cpp") == std::optional<std::string> {""});
  CHECK(summary_of("shapes::perimeter", "src/area.cpp") == std::optional<std::string> {""});
  CHECK(
    summary_of("shapes::Circle", "src/area.hpp")
    == std::optional<std::string> {"A circle given by its radius."}
  );
  CHECK(
    summary_of("shapes::Circle::radius", "src/area.hpp")
    == std::optional<std::string> {"Distance from the centre to the edge."}
  );
  CHECK(summary_of("shapes::Circle::unused", "src/area.hpp") == std::optional<std::string> {""});
  CHECK(
    summary_of("src/area.hpp", "src/area.hpp")
    == std::optional<std::string> {"Area formulas for simple shapes."}
  );
}
