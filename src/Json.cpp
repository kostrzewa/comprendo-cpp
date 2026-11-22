/// \file
/// \brief Strict JSON parser and compact serializer.

#include "Comprendo/Json.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>

namespace
{
  using Comprendo::Json;

  /// Maximum nesting depth accepted by the parser.
  constexpr int kMaxDepth = 256;

  /// Largest integer magnitude a double represents exactly (2^53).
  constexpr double kMaxExactInteger = 9007199254740992.0;

  /// First code point of the UTF-16 high-surrogate range.
  constexpr std::uint32_t kHighSurrogateFirst = 0xD800;
  /// First code point of the UTF-16 low-surrogate range.
  constexpr std::uint32_t kLowSurrogateFirst = 0xDC00;
  /// One past the last code point of the UTF-16 surrogate ranges.
  constexpr std::uint32_t kSurrogateEnd = 0xE000;

  /// Recursive-descent parser over a JSON text.
  class Parser
  {
  public:
    /// Prepare to parse \p text.
    ///
    /// \param text Complete JSON document.
    explicit Parser(std::string_view text) : m_text(text) { }

    /// Parse the document.
    ///
    /// \return The value, or an error message.
    std::expected<Json, std::string>
    parse_document()
    {
      auto value = parse_value(0);
      if (!value)
      {
        return value;
      }
      skip_whitespace();
      if (m_pos != m_text.size())
      {
        return fail("unexpected trailing characters");
      }
      return value;
    }

  private:
    /// Build an error result at the current offset.
    ///
    /// \param message Description of the problem.
    /// \return Unexpected error.
    [[nodiscard]]
    std::unexpected<std::string>
    fail(std::string_view message) const
    {
      return std::unexpected(fmt::format("JSON error at offset {}: {}", m_pos, message));
    }

    /// Advance past spaces, tabs, and line breaks.
    void
    skip_whitespace()
    {
      while (m_pos < m_text.size())
      {
        const char c = m_text[m_pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        {
          return;
        }
        ++m_pos;
      }
    }

    /// Consume a literal keyword.
    ///
    /// \param word Expected keyword.
    /// \return True when the keyword was present and consumed.
    bool
    consume(std::string_view word)
    {
      if (m_text.substr(m_pos, word.size()) != word)
      {
        return false;
      }
      m_pos += word.size();
      return true;
    }

    /// Parse any value.
    ///
    /// \param depth Current nesting depth.
    /// \return The value, or an error message.
    std::expected<Json, std::string>
    parse_value(int depth)
    {
      if (depth > kMaxDepth)
      {
        return fail("nesting too deep");
      }
      skip_whitespace();
      if (m_pos >= m_text.size())
      {
        return fail("unexpected end of input");
      }
      switch (m_text[m_pos])
      {
      case '{': return parse_object(depth);
      case '[': return parse_array(depth);
      case '"':
        return parse_string().transform([](std::string text) { return Json {std::move(text)}; });
      case 't':
        return consume("true") ? std::expected<Json, std::string> {Json {true}}
                               : fail("bad literal");
      case 'f':
        return consume("false") ? std::expected<Json, std::string> {Json {false}}
                                : fail("bad literal");
      case 'n':
        return consume("null") ? std::expected<Json, std::string> {Json {}} : fail("bad literal");
      default: return parse_number();
      }
    }

    /// Parse an object starting at '{'.
    ///
    /// \param depth Current nesting depth.
    /// \return The object, or an error message.
    std::expected<Json, std::string>
    parse_object(int depth)
    {
      ++m_pos;
      Json::Object members;
      skip_whitespace();
      if (consume("}"))
      {
        return Json {std::move(members)};
      }
      while (true)
      {
        skip_whitespace();
        if (m_pos >= m_text.size() || m_text[m_pos] != '"')
        {
          return fail("expected object key");
        }
        auto key = parse_string();
        if (!key)
        {
          return std::unexpected(key.error());
        }
        skip_whitespace();
        if (!consume(":"))
        {
          return fail("expected ':'");
        }
        auto value = parse_value(depth + 1);
        if (!value)
        {
          return value;
        }
        members.emplace_back(std::move(*key), std::move(*value));
        skip_whitespace();
        if (consume("}"))
        {
          return Json {std::move(members)};
        }
        if (!consume(","))
        {
          return fail("expected ',' or '}'");
        }
      }
    }

    /// Parse an array starting at '['.
    ///
    /// \param depth Current nesting depth.
    /// \return The array, or an error message.
    std::expected<Json, std::string>
    parse_array(int depth)
    {
      ++m_pos;
      Json::Array elements;
      skip_whitespace();
      if (consume("]"))
      {
        return Json {std::move(elements)};
      }
      while (true)
      {
        auto value = parse_value(depth + 1);
        if (!value)
        {
          return value;
        }
        elements.push_back(std::move(*value));
        skip_whitespace();
        if (consume("]"))
        {
          return Json {std::move(elements)};
        }
        if (!consume(","))
        {
          return fail("expected ',' or ']'");
        }
      }
    }

    /// Parse four hexadecimal digits of a \\u escape.
    ///
    /// \return The code unit, or an error message.
    std::expected<std::uint32_t, std::string>
    parse_hex4()
    {
      constexpr std::size_t kDigits = 4;
      if (m_pos + kDigits > m_text.size())
      {
        return fail("truncated \\u escape");
      }
      std::uint32_t unit = 0;
      const char *first = m_text.data() + m_pos;
      const auto [end, error] = std::from_chars(first, first + kDigits, unit, 16);
      if (error != std::errc {} || end != first + kDigits)
      {
        return fail("invalid \\u escape");
      }
      m_pos += kDigits;
      return unit;
    }

    /// Append a code point to a string as UTF-8.
    ///
    /// \param out Destination.
    /// \param code_point Unicode scalar value.
    static void
    append_utf8(std::string &out, std::uint32_t code_point)
    {
      constexpr std::uint32_t kOneByteEnd = 0x80;
      constexpr std::uint32_t kTwoByteEnd = 0x800;
      constexpr std::uint32_t kThreeByteEnd = 0x10000;
      constexpr std::uint32_t kContinuation = 0x80;
      constexpr std::uint32_t kSixBits = 0x3F;
      if (code_point < kOneByteEnd)
      {
        out.push_back(static_cast<char>(code_point));
      }
      else if (code_point < kTwoByteEnd)
      {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(kContinuation | (code_point & kSixBits)));
      }
      else if (code_point < kThreeByteEnd)
      {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(kContinuation | ((code_point >> 6) & kSixBits)));
        out.push_back(static_cast<char>(kContinuation | (code_point & kSixBits)));
      }
      else
      {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(kContinuation | ((code_point >> 12) & kSixBits)));
        out.push_back(static_cast<char>(kContinuation | ((code_point >> 6) & kSixBits)));
        out.push_back(static_cast<char>(kContinuation | (code_point & kSixBits)));
      }
    }

    /// Parse a \\u escape, combining surrogate pairs.
    ///
    /// \param out Destination for the UTF-8 encoding.
    /// \return Nothing, or an error message.
    std::expected<void, std::string>
    parse_unicode_escape(std::string &out)
    {
      auto unit = parse_hex4();
      if (!unit)
      {
        return std::unexpected(unit.error());
      }
      std::uint32_t code_point = *unit;
      if (code_point >= kLowSurrogateFirst && code_point < kSurrogateEnd)
      {
        return fail("unpaired low surrogate");
      }
      if (code_point >= kHighSurrogateFirst && code_point < kLowSurrogateFirst)
      {
        if (!consume("\\u"))
        {
          return fail("unpaired high surrogate");
        }
        auto low = parse_hex4();
        if (!low)
        {
          return std::unexpected(low.error());
        }
        if (*low < kLowSurrogateFirst || *low >= kSurrogateEnd)
        {
          return fail("invalid low surrogate");
        }
        constexpr std::uint32_t kSupplementaryBase = 0x10000;
        code_point = kSupplementaryBase + ((code_point - kHighSurrogateFirst) << 10)
          + (*low - kLowSurrogateFirst);
      }
      append_utf8(out, code_point);
      return {};
    }

    /// Parse a string starting at '"'.
    ///
    /// \return The decoded UTF-8 text, or an error message.
    std::expected<std::string, std::string>
    parse_string()
    {
      ++m_pos;
      std::string out;
      while (m_pos < m_text.size())
      {
        const char c = m_text[m_pos++];
        if (c == '"')
        {
          return out;
        }
        if (static_cast<unsigned char>(c) < 0x20)
        {
          return fail("control character in string");
        }
        if (c != '\\')
        {
          out.push_back(c);
          continue;
        }
        if (m_pos >= m_text.size())
        {
          break;
        }
        const char escape = m_text[m_pos++];
        switch (escape)
        {
        case '"':
        case '\\':
        case '/': out.push_back(escape); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u':
          if (auto decoded = parse_unicode_escape(out); !decoded)
          {
            return std::unexpected(decoded.error());
          }
          break;
        default: return fail("invalid escape");
        }
      }
      return fail("unterminated string");
    }

    /// Advance over a run of decimal digits.
    ///
    /// \return Number of digits consumed.
    std::size_t
    skip_digits()
    {
      const std::size_t start = m_pos;
      while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9')
      {
        ++m_pos;
      }
      return m_pos - start;
    }

    /// Parse a number, enforcing the JSON grammar before converting.
    ///
    /// \return The number, or an error message.
    std::expected<Json, std::string>
    parse_number()
    {
      const std::size_t start = m_pos;
      consume("-");
      if (consume("0"))
      {
        if (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9')
        {
          return fail("leading zero in number");
        }
      }
      else if (skip_digits() == 0)
      {
        return fail("unexpected character");
      }
      if (consume(".") && skip_digits() == 0)
      {
        return fail("expected digits after '.'");
      }
      if (consume("e") || consume("E"))
      {
        if (!consume("+"))
        {
          consume("-");
        }
        if (skip_digits() == 0)
        {
          return fail("expected exponent digits");
        }
      }
      double value = 0.0;
      const char *first = m_text.data() + start;
      const char *last = m_text.data() + m_pos;
      const auto [end, error] = std::from_chars(first, last, value);
      if (error != std::errc {} || end != last)
      {
        return fail("number out of range");
      }
      return Json {value};
    }

    /// Text being parsed.
    std::string_view m_text;
    /// Current byte offset.
    std::size_t m_pos = 0;
  };

  /// Append a JSON string literal.
  ///
  /// \param out Destination.
  /// \param text UTF-8 text to quote and escape.
  void
  dump_string(std::string &out, std::string_view text)
  {
    out.push_back('"');
    for (const char c : text)
    {
      switch (c)
      {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20)
        {
          out += fmt::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
        }
        else
        {
          out.push_back(c);
        }
      }
    }
    out.push_back('"');
  }

  /// Append a JSON number; integral values print without a fraction.
  ///
  /// \param out Destination.
  /// \param value Finite number; non-finite values print as null.
  void
  dump_number(std::string &out, double value)
  {
    if (!std::isfinite(value))
    {
      out += "null";
      return;
    }
    if (std::trunc(value) == value && std::fabs(value) <= kMaxExactInteger)
    {
      out += fmt::format("{}", static_cast<std::int64_t>(value));
      return;
    }
    constexpr std::size_t kBufferSize = 32;
    std::array<char, kBufferSize> buffer {};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), error == std::errc {} ? end : buffer.data());
  }

  /// Append a value's JSON text.
  ///
  /// \param out Destination.
  /// \param value Value to serialize.
  void
  dump_value(std::string &out, const Json &value)
  {
    if (value.is_null())
    {
      out += "null";
    }
    else if (const bool *flag = value.as_bool())
    {
      out += *flag ? "true" : "false";
    }
    else if (const double *number = value.as_number())
    {
      dump_number(out, *number);
    }
    else if (const std::string *text = value.as_string())
    {
      dump_string(out, *text);
    }
    else if (const Json::Array *array = value.as_array())
    {
      out.push_back('[');
      for (std::size_t i = 0; i < array->size(); ++i)
      {
        if (i > 0)
        {
          out.push_back(',');
        }
        dump_value(out, (*array)[i]);
      }
      out.push_back(']');
    }
    else if (const Json::Object *object = value.as_object())
    {
      out.push_back('{');
      for (std::size_t i = 0; i < object->size(); ++i)
      {
        if (i > 0)
        {
          out.push_back(',');
        }
        dump_string(out, (*object)[i].first);
        out.push_back(':');
        dump_value(out, (*object)[i].second);
      }
      out.push_back('}');
    }
  }
} // namespace

namespace Comprendo
{
  std::expected<Json, std::string>
  Json::parse(std::string_view text)
  {
    return Parser {text}.parse_document();
  }

  std::string
  Json::dump() const
  {
    std::string out;
    dump_value(out, *this);
    return out;
  }

  const Json *
  Json::find(std::string_view key) const
  {
    const Object *object = as_object();
    if (object == nullptr)
    {
      return nullptr;
    }
    for (const auto &[name, value] : *object)
    {
      if (name == key)
      {
        return &value;
      }
    }
    return nullptr;
  }

  std::string_view
  Json::string_or_empty(std::string_view key) const
  {
    const Json *member = find(key);
    const std::string *text = member == nullptr ? nullptr : member->as_string();
    return text == nullptr ? std::string_view {} : std::string_view {*text};
  }
} // namespace Comprendo
