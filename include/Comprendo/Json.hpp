#pragma once

/// \file
/// \brief Minimal JSON value with a strict RFC 8259 parser and a compact serializer.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Comprendo
{
  /// A JSON value: null, boolean, number, string, array, or object.
  ///
  /// Objects keep their members in insertion order. Numbers are stored as double,
  /// which represents every integer ctags or the LSP protocol emits exactly.
  class Json
  {
  public:
    /// JSON array.
    using Array = std::vector<Json>;
    /// JSON object as ordered key/value pairs.
    using Object = std::vector<std::pair<std::string, Json>>;

    /// Construct null.
    Json() = default;

    /// Construct null.
    Json(std::nullptr_t) { }

    /// Construct a boolean.
    ///
    /// \param value Boolean value.
    Json(bool value) : m_value(value) { }

    /// Construct a number.
    ///
    /// \param value Numeric value.
    Json(double value) : m_value(value) { }

    /// Construct a number from an integer.
    ///
    /// \param value Integer value; must be exactly representable as double.
    Json(std::int64_t value) : m_value(static_cast<double>(value)) { }

    /// Construct a number from an int.
    ///
    /// \param value Integer value.
    Json(int value) : m_value(static_cast<double>(value)) { }

    /// Construct a string.
    ///
    /// \param value UTF-8 text.
    Json(std::string value) : m_value(std::move(value)) { }

    /// Construct a string.
    ///
    /// \param value UTF-8 text.
    Json(std::string_view value) : m_value(std::string {value}) { }

    /// Construct a string.
    ///
    /// \param value NUL-terminated UTF-8 text.
    Json(const char *value) : m_value(std::string {value}) { }

    /// Construct an array.
    ///
    /// \param value Array elements.
    Json(Array value) : m_value(std::move(value)) { }

    /// Construct an object.
    ///
    /// \param value Object members.
    Json(Object value) : m_value(std::move(value)) { }

    /// Parse a complete JSON document.
    ///
    /// \param text JSON text; trailing whitespace is allowed, anything else is not.
    /// \return The parsed value, or an error message with the byte offset.
    [[nodiscard]]
    static std::expected<Json, std::string>
    parse(std::string_view text);

    /// Serialize to compact JSON text.
    ///
    /// \return JSON text without insignificant whitespace.
    [[nodiscard]]
    std::string
    dump() const;

    /// Check for null.
    ///
    /// \return True when the value is null.
    [[nodiscard]]
    bool
    is_null() const
    {
      return std::holds_alternative<std::nullptr_t>(m_value);
    }

    /// Access a boolean.
    ///
    /// \return Pointer to the boolean, or null when the value is not a boolean.
    [[nodiscard]]
    const bool *
    as_bool() const
    {
      return std::get_if<bool>(&m_value);
    }

    /// Access a number.
    ///
    /// \return Pointer to the number, or null when the value is not a number.
    [[nodiscard]]
    const double *
    as_number() const
    {
      return std::get_if<double>(&m_value);
    }

    /// Access a string.
    ///
    /// \return Pointer to the string, or null when the value is not a string.
    [[nodiscard]]
    const std::string *
    as_string() const
    {
      return std::get_if<std::string>(&m_value);
    }

    /// Access an array.
    ///
    /// \return Pointer to the array, or null when the value is not an array.
    [[nodiscard]]
    const Array *
    as_array() const
    {
      return std::get_if<Array>(&m_value);
    }

    /// Access an object.
    ///
    /// \return Pointer to the members, or null when the value is not an object.
    [[nodiscard]]
    const Object *
    as_object() const
    {
      return std::get_if<Object>(&m_value);
    }

    /// Look up an object member.
    ///
    /// \param key Member name.
    /// \return Pointer to the first member named \p key, or null when absent or
    ///         when the value is not an object.
    [[nodiscard]]
    const Json *
    find(std::string_view key) const;

    /// Look up a string member.
    ///
    /// \param key Member name.
    /// \return The member's text, or empty when absent or not a string.
    [[nodiscard]]
    std::string_view
    string_or_empty(std::string_view key) const;

    /// Memberwise equality; object member order is significant.

    ///

    /// \return True when all members are equal.
    bool
    operator==(const Json &) const = default;

  private:
    /// Stored alternative.
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> m_value = nullptr;
  };
} // namespace Comprendo
