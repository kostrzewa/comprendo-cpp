/// \file
/// \brief Tests for the JSON parser and serializer.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "Comprendo/Json.hpp"

using Catch::Matchers::ContainsSubstring;
using Comprendo::Json;

/// Scalars parse to the matching alternative.
TEST_CASE("json parses scalars", "[json]")
{
  CHECK(Json::parse("null")->is_null());
  CHECK(*Json::parse("true")->as_bool());
  CHECK_FALSE(*Json::parse(" false ")->as_bool());
  CHECK(*Json::parse("-12.5e1")->as_number() == -125.0);
  CHECK(*Json::parse("0")->as_number() == 0.0);
  CHECK(*Json::parse(R"("a\"b\\c\/d\n")")->as_string() == "a\"b\\c/d\n");
}

/// Unicode escapes decode to UTF-8, including surrogate pairs.
TEST_CASE("json decodes unicode escapes", "[json]")
{
  CHECK(*Json::parse(R"("\u00e9")")->as_string() == "\xc3\xa9");
  CHECK(*Json::parse(R"("\u20ac")")->as_string() == "\xe2\x82\xac");
  CHECK(*Json::parse(R"("\ud83e\udd80")")->as_string() == "\xf0\x9f\xa6\x80");
  CHECK(*Json::parse("\"z\xc3\xb3\"")->as_string() == "z\xc3\xb3");
}

/// Objects keep member order and support lookup.
TEST_CASE("json parses nested containers", "[json]")
{
  const auto value = Json::parse(R"({"b": [1, {"c": null}], "a": "x", "b": 2})");
  REQUIRE(value.has_value());
  const Json::Object *object = value->as_object();
  REQUIRE(object != nullptr);
  REQUIRE(object->size() == 3);
  CHECK((*object)[0].first == "b");
  CHECK((*object)[1].first == "a");
  CHECK(value->string_or_empty("a") == "x");
  CHECK(value->string_or_empty("missing").empty());
  CHECK(value->find("b")->as_array()->size() == 2);
  CHECK(value->find("b")->as_array()->at(1).find("c")->is_null());
}

/// Malformed documents are rejected with an offset.
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
    REQUIRE_FALSE(parsed.has_value());
    CHECK_THAT(parsed.error(), ContainsSubstring("JSON error at offset"));
  }
}

/// Excessive nesting is rejected instead of overflowing the stack.
TEST_CASE("json limits nesting depth", "[json]")
{
  const std::string deep = std::string(1000, '[') + std::string(1000, ']');
  const auto parsed = Json::parse(deep);
  REQUIRE_FALSE(parsed.has_value());
  CHECK_THAT(parsed.error(), ContainsSubstring("nesting too deep"));
}

/// Serialization is compact, escapes control characters, and round-trips.
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
  REQUIRE(reparsed.has_value());
  CHECK(*reparsed == value);
}
