/// \file
/// \brief Tests for the MCP stdio server that offers the chart tools to assistants.

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "Comprendo/ChartTools.hpp"
#include "Comprendo/Json.hpp"
#include "Comprendo/Mcp.hpp"
#include "TempDir.hpp"

namespace
{
  using Comprendo::Json;

  /// Handle one message and parse the reply.
  ///
  /// \param tools Chart tools.
  /// \param line Message.
  /// \return Parsed reply, or null when there was none or it was not JSON.
  Json
  exchange(const Comprendo::ChartTools &tools, std::string_view line)
  {
    const std::optional<std::string> reply = Comprendo::handle_mcp_message(line, tools);
    if (!reply)
    {
      return {};
    }
    return Json::parse(*reply).value_or(Json {});
  }

  /// Follow a path of object members.
  ///
  /// \param value Root value.
  /// \param keys Member names, outermost first.
  /// \return The member, or null when a step is missing.
  const Json *
  at(const Json &value, std::initializer_list<std::string_view> keys)
  {
    const Json *current = &value;
    for (const std::string_view key : keys)
    {
      current = current != nullptr ? current->find(key) : nullptr;
    }
    return current;
  }

  /// Copy of a nested member.
  ///
  /// \param value Root value.
  /// \param keys Member names, outermost first.
  /// \return The member, or null when a step is missing.
  Json
  value_at(const Json &value, std::initializer_list<std::string_view> keys)
  {
    const Json *found = at(value, keys);
    return found != nullptr ? *found : Json {};
  }

  /// Text of a string member of a nested object.
  ///
  /// \param value Root value.
  /// \param keys Member names leading to the object.
  /// \param member String member of that object.
  /// \return The text, or empty when anything is missing.
  std::string
  text_at(const Json &value, std::initializer_list<std::string_view> keys, std::string_view member)
  {
    const Json *object = at(value, keys);
    return object != nullptr ? std::string {object->string_or_empty(member)} : std::string {};
  }

  /// Error code of a reply.
  ///
  /// \param reply Parsed reply.
  /// \return The code, or 0 when the reply is not an error.
  double
  error_code(const Json &reply)
  {
    const Json *code = at(reply, {"error", "code"});
    return code != nullptr && code->as_number() != nullptr ? *code->as_number() : 0.0;
  }
} // namespace

/// Clients refuse servers that do not complete the handshake in a protocol
/// revision they speak. Sends initialize with a known and an unknown revision
/// and checks the echoed or newest revision, the tools capability, and the
/// server name; then checks notifications get no reply and ping answers.
TEST_CASE("MCP handshake negotiates the protocol revision", "[mcp]")
{
  const Comprendo::Testing::TempDir project {"mcp-handshake"};
  const Comprendo::ChartTools tools {project.path()};
  const Json known = exchange(
    tools,
    R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})"
  );
  CHECK(text_at(known, {"result"}, "protocolVersion") == "2025-06-18");
  CHECK(at(known, {"result", "capabilities", "tools"}) != nullptr);
  CHECK(text_at(known, {"result", "serverInfo"}, "name") == "comprendo");
  const Json unknown = exchange(
    tools,
    R"({"jsonrpc":"2.0","id":"a","method":"initialize","params":{"protocolVersion":"1999-01-01"}})"
  );
  CHECK(text_at(unknown, {"result"}, "protocolVersion") == "2025-11-25");
  CHECK(unknown.string_or_empty("id") == "a");
  CHECK_FALSE(
    Comprendo::handle_mcp_message(
      R"({"jsonrpc":"2.0","method":"notifications/initialized"})", tools
    )
  );
  CHECK(at(exchange(tools, R"({"jsonrpc":"2.0","id":2,"method":"ping"})"), {"result"}) != nullptr);
}

/// The model sees the tools through tools/list and uses them through
/// tools/call; tool mistakes must arrive as results it can read, protocol
/// mistakes as JSON-RPC errors. Lists the tools, runs a successful and a
/// failing call, and checks unknown tools, unknown methods, bad JSON, and a
/// non-object message get the right error codes.
TEST_CASE("MCP lists and calls the chart tools", "[mcp]")
{
  const Comprendo::Testing::TempDir project {"mcp-tools"};
  const Comprendo::ChartTools tools {project.path()};
  const Json listed = exchange(tools, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
  const Json *entries = at(listed, {"result", "tools"});
  CHECK(entries != nullptr);
  if (entries == nullptr || entries->as_array() == nullptr)
  {
    return;
  }
  CHECK(entries->as_array()->size() == 7);
  CHECK(entries->as_array()->front().string_or_empty("name") == "chart_overview");
  CHECK(text_at(entries->as_array()->front(), {"inputSchema"}, "type") == "object");

  const Json overview = exchange(
    tools, R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"chart_overview"}})"
  );
  CHECK(value_at(overview, {"result", "isError"}) == Json {false});
  const Json failed = exchange(
    tools,
    R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"chart_annotate_node","arguments":{"id":"0x1","summary":"x"}}})"
  );
  CHECK(value_at(failed, {"result", "isError"}) == Json {true});

  CHECK(
    error_code(
      exchange(tools, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"nope"}})")
    )
    == -32602
  );
  CHECK(
    error_code(exchange(tools, R"({"jsonrpc":"2.0","id":5,"method":"resources/list"})")) == -32601
  );
  const Json bad = exchange(tools, "{not json");
  CHECK(error_code(bad) == -32700);
  CHECK(at(bad, {"id"}) != nullptr);
  CHECK(value_at(bad, {"id"}).is_null());
  CHECK(error_code(exchange(tools, "[1, 2]")) == -32600);
}

/// The stdio transport is one message per line; blank lines and notifications
/// must not produce output, or the client loses track of the replies. Feeds a
/// request, a notification, a blank line, and a CRLF-terminated request
/// through serve_mcp() and checks exactly two reply lines come back.
TEST_CASE("serve_mcp answers each request on its own line", "[mcp]")
{
  const Comprendo::Testing::TempDir project {"mcp-serve"};
  const Comprendo::ChartTools tools {project.path()};
  std::istringstream in {"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n"
                         "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
                         "\n"
                         "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}\r\n"};
  std::ostringstream out;
  Comprendo::serve_mcp(in, out, tools);
  CHECK(
    out.str()
    == "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}"
       "\n"
  );
}
