/// \file
/// \brief Tests for the JSON parser and serializer.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "Checks.hpp"
#include "Comprendo/Json.hpp"

using Catch::Matchers::ContainsSubstring;
using Comprendo::Json;

/// ctags output and LSP messages are JSON, and the in-tree parser replaces a
/// library, so every scalar form must decode correctly. Parses null, true, false
/// (with surrounding whitespace), numbers with sign, fraction, and exponent, and a
/// string with escaped quote, backslash, slash, and newline, and checks each value.
TEST_CASE("json parses scalars", "[json]")
{
  CHECK(Json::parse("null") == Json {});
  CHECK(Json::parse("true") == Json {true});
  CHECK(Json::parse(" false ") == Json {false});
  CHECK(Json::parse("-12.5e1") == Json {-125.0});
  CHECK(Json::parse("0") == Json {0});
  CHECK(Json::parse(R"("a\"b\\c\/d\n")") == Json {"a\"b\\c/d\n"});
}

/// Symbol names and comments may contain non-ASCII text, which JSON may carry as
/// \u escapes. Decodes two-byte and three-byte code points, a surrogate pair
/// encoding a four-byte character, and raw UTF-8 passed through unchanged, and
/// compares the resulting bytes.
TEST_CASE("json decodes unicode escapes", "[json]")
{
  CHECK(Json::parse(R"("\u00e9")") == Json {"\xc3\xa9"});
  CHECK(Json::parse(R"("\u20ac")") == Json {"\xe2\x82\xac"});
  CHECK(Json::parse(R"("\ud83e\udd80")") == Json {"\xf0\x9f\xa6\x80"});
  CHECK(Json::parse("\"z\xc3\xb3\"") == Json {"z\xc3\xb3"});
}

/// ctags records are objects and LSP payloads nest arrays and objects, so lookup
/// and member order must behave predictably. Parses an object with a nested array
/// and object and a duplicated key, then checks member order is preserved,
/// find() returns the first match, and nested values are reachable.
TEST_CASE("json parses nested containers", "[json]")
{
  const auto value = Json::parse(R"({"b": [1, {"c": null}], "a": "x", "b": 2})");
  const Json first_b {Json::Array {1, Json::Object {{"c", nullptr}}}};
  CHECK(value == Json {Json::Object {{"b", first_b}, {"a", "x"}, {"b", 2}}});
  if (!value)
  {
    return;
  }
  CHECK(value->string_or_empty("a") == "x");
  CHECK(value->string_or_empty("missing").empty());
  const Json *found = value->find("b");
  CHECK(found != nullptr);
  if (found == nullptr)
  {
    return;
  }
  CHECK(*found == first_b);
}

/// Malformed tool output must produce an error with a position, never a crash or a
/// silently truncated value. Parses a table of invalid documents (trailing commas,
/// leading zeros, incomplete numbers, unterminated strings, raw control
/// characters, bad escapes, lone surrogates, bad literals, trailing data, non-string
/// keys) and checks each fails with an offset in the message.
TEST_CASE("json rejects malformed input", "[json]")
{
  const char *const bad[] = {
    "",
    "[1,]",
    "{\"a\" 1}",
    "{\"a\":1,}",
    "01",
    "1.",
    "1e",
    "-",
    "\"unterminated",
    "\"tab\there\"",
    "\"\\x\"",
    "\"\\ud800\"",
    "\"\\udc00\"",
    "\"\\ud800\\u0041\"",
    "tru",
    "nul",
    "[1] 2",
    "{1: 2}",
  };
  for (const char *text : bad)
  {
    INFO("input: " << text);
    const auto parsed = Json::parse(text);
    CHECK_FALSE(parsed.has_value());
    CHECK_THAT(Comprendo::Testing::error_of(parsed), ContainsSubstring("JSON error at offset"));
  }
}

/// The parser is recursive, so hostile or broken input could overflow the stack.
/// Parses 1000 nested arrays, well beyond the 256-level limit, and checks it is
/// rejected with a nesting error instead of crashing.
TEST_CASE("json limits nesting depth", "[json]")
{
  const std::string deep = std::string(1000, '[') + std::string(1000, ']');
  const auto parsed = Json::parse(deep);
  CHECK_FALSE(parsed.has_value());
  CHECK_THAT(Comprendo::Testing::error_of(parsed), ContainsSubstring("nesting too deep"));
}

/// The LSP client (M2b) will send JSON built with this class, so serialization
/// must be valid, compact, and lossless. Dumps an object with integers, a
/// fraction, a negative number, escaped and control characters, booleans, null,
/// and an empty object, compares the exact text, and parses it back to the same value.
TEST_CASE("json dumps and round-trips", "[json]")
{
  const Json value {Json::Object {
    {"id", 7},
    {"pi", 0.5},
    {"neg", -3},
    {"text", "line\n\"quoted\"\x01"},
    {"list", Json::Array {true, false, nullptr, "x"}},
    {"empty", Json::Object {}},
  }};
  const std::string text = value.dump();
  CHECK(
    text
    == R"({"id":7,"pi":0.5,"neg":-3,"text":"line\n\"quoted\"\u0001","list":[true,false,null,"x"],"empty":{}})"
  );
  const auto reparsed = Json::parse(text);
  CHECK(reparsed == value);
}
