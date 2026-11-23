/// \file
/// \brief Tests for the Source panel's C++ highlighter and source file lookup.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "Comprendo/SourceHighlight.hpp"
#include "TempDir.hpp"

namespace
{
  using Comprendo::LexState;
  using Comprendo::Token;
  using Comprendo::TokenKind;

  /// A span as text and kind, for readable expectations.
  struct Span
  {
    /// Text of the span.
    std::string text;
    /// Kind of the span.
    TokenKind kind = TokenKind::Text;

    /// Memberwise equality.
    ///
    /// \return True when all members are equal.
    bool
    operator==(const Span &) const = default;
  };

  /// Highlight a line and return its non-Text spans.
  ///
  /// \param line Line.
  /// \param state Lexer state, carried across calls.
  /// \return Spans other than plain text.
  std::vector<Span>
  marked(std::string_view line, LexState &state)
  {
    std::vector<Span> spans;
    for (const Token &token : Comprendo::highlight_line(line, state))
    {
      if (token.kind != TokenKind::Text)
      {
        spans.push_back(
          {std::string {line.substr(token.begin, token.end - token.begin)}, token.kind}
        );
      }
    }
    return spans;
  }

  /// Highlight a single line with a fresh state.
  ///
  /// \param line Line.
  /// \return Spans other than plain text.
  std::vector<Span>
  marked(std::string_view line)
  {
    LexState state;
    return marked(line, state);
  }
} // namespace

/// The Source panel colours keywords, types, literals, and comments so code
/// reads like in an editor. Checks a declaration (keywords, a type, a number
/// with digit separators and suffix, a comment), strings with escapes and
/// prefixes, and character literals versus digit separators.
TEST_CASE("C++ tokens are classified", "[source]")
{
  CHECK(
    marked("static constexpr int kSize = 1'024u; // bytes")
    == std::vector<Span> {
      {"static", TokenKind::Keyword},
      {"constexpr", TokenKind::Keyword},
      {"int", TokenKind::Type},
      {"1'024u", TokenKind::Number},
      {"// bytes", TokenKind::Comment},
    }
  );
  CHECK(
    marked(R"(auto s = u8"a \"quoted\" word"; char c = '\'';)")
    == std::vector<Span> {
      {"auto", TokenKind::Keyword},
      {R"(u8"a \"quoted\" word")", TokenKind::String},
      {"char", TokenKind::Type},
      {R"('\'')", TokenKind::Character},
    }
  );
  CHECK(
    marked("x = 1.5e-3f + .25;")
    == std::vector<Span> {{"1.5e-3f", TokenKind::Number}, {".25", TokenKind::Number}}
  );
  CHECK(marked("value_type u8x") == std::vector<Span> {});
}

/// Block comments and raw strings span lines, so the lexer must carry its
/// state from one line to the next. Lexes a three-line block comment and a
/// raw string with a custom delimiter whose closing appears on a later line,
/// checking the code after each closing is lexed normally again.
TEST_CASE("comments and raw strings continue across lines", "[source]")
{
  LexState state;
  CHECK(
    marked("int a; /* start", state)
    == std::vector<Span> {{"int", TokenKind::Type}, {"/* start", TokenKind::Comment}}
  );
  CHECK(
    marked("  still comment", state) == std::vector<Span> {{"  still comment", TokenKind::Comment}}
  );
  CHECK(
    marked("end */ return 0;", state)
    == std::vector<Span> {
      {"end */", TokenKind::Comment}, {"return", TokenKind::Keyword}, {"0", TokenKind::Number}
    }
  );
  CHECK_FALSE(state.in_block_comment);

  CHECK(
    marked(R"(auto sql = R"x(SELECT ")", state)
    == std::vector<Span> {{"auto", TokenKind::Keyword}, {R"(R"x(SELECT ")", TokenKind::String}}
  );
  CHECK(state.raw_delimiter == std::string {"x"});
  CHECK(
    marked(R"( FROM t)x"; if)", state)
    == std::vector<Span> {{R"( FROM t)x")", TokenKind::String}, {"if", TokenKind::Keyword}}
  );
  CHECK_FALSE(state.raw_delimiter.has_value());
}

/// Preprocessor lines look different from code, and include paths in angle
/// brackets are names, not comparisons. Checks an include with angle
/// brackets, a quoted include, and a define whose value is still lexed.
TEST_CASE("preprocessor directives", "[source]")
{
  CHECK(
    marked("#include <vector>")
    == std::vector<Span> {{"#include", TokenKind::Preprocessor}, {"<vector>", TokenKind::String}}
  );
  CHECK(
    marked("  #  include \"a.hpp\"")
    == std::vector<Span> {{"#  include", TokenKind::Preprocessor}, {"\"a.hpp\"", TokenKind::String}}
  );
  CHECK(
    marked("#define SCALE 2")
    == std::vector<Span> {{"#define", TokenKind::Preprocessor}, {"2", TokenKind::Number}}
  );
}

/// Whole files are shown with line numbers, so lines must split on both line
/// endings without inventing a trailing empty line, and tabs must expand to
/// tab stops so columns line up. Highlights a small CRLF file with a tab.
TEST_CASE("files split into lines with tabs expanded", "[source]")
{
  const auto source = Comprendo::highlight_source("int a;\r\n\tint b; // x\r\n", 4);
  CHECK(source.lines == std::vector<std::string> {"int a;", "    int b; // x"});
  CHECK(source.tokens.size() == 2);
  CHECK(Comprendo::highlight_source("").lines == std::vector<std::string> {""});
}

/// Chart paths are repository-relative and the viewer usually runs from the
/// source tree, but the chart's recorded root and its own location are
/// fallbacks. Creates a file under a second root and checks the first root
/// that has it wins and missing files resolve to nothing.
TEST_CASE("source files are found under the first root that has them", "[source]")
{
  const Comprendo::Testing::TempDir dir {"source-resolve"};
  std::filesystem::create_directories(dir / "b/src");
  std::ofstream {dir / "b/src/x.cpp"} << "int x;\n";
  const std::vector<std::filesystem::path> roots = {dir / "a", {}, dir / "b"};
  CHECK(Comprendo::resolve_source("src/x.cpp", roots) == (dir / "b/src/x.cpp").lexically_normal());
  CHECK_FALSE(Comprendo::resolve_source("src/y.cpp", roots));
  CHECK_FALSE(Comprendo::resolve_source("", roots));
}
