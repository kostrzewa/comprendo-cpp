/// \file
/// \brief MCP stdio server: JSON-RPC framing, the initialize handshake, and tool calls.

#include "Comprendo/Mcp.hpp"

#include <algorithm>
#include <array>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "Comprendo/ChartTools.hpp"
#include "Comprendo/Json.hpp"
#include "Logger/Logger.hpp"
#include "Version.hpp"

namespace
{
  using Comprendo::Json;

  /// Protocol revisions this server speaks, oldest first.
  constexpr std::array<std::string_view, 4> kProtocolVersions = {
    "2024-11-05",
    "2025-03-26",
    "2025-06-18",
    "2025-11-25",
  };

  /// JSON-RPC: the message is not valid JSON.
  constexpr int kParseError = -32700;
  /// JSON-RPC: the message is not a request object.
  constexpr int kInvalidRequest = -32600;
  /// JSON-RPC: the method does not exist.
  constexpr int kMethodNotFound = -32601;
  /// JSON-RPC: the parameters are invalid.
  constexpr int kInvalidParams = -32602;

  /// Tells the model what the server is for.
  constexpr std::string_view kInstructions = "Tools for building a Comprendo code chart "
                                             "(.comprendo/chart.sqlite): look up nodes, "
                                             "annotate them, group them into clusters, add missing "
                                             "symbols and relationships. Start "
                                             "with chart_overview and end with chart_finish.";

  /// Build a successful reply.
  ///
  /// \param id Request id.
  /// \param result Result value.
  /// \return Reply line.
  std::string
  reply(const Json &id, Json result)
  {
    return Json {
      Json::Object {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", std::move(result)},
      }
    }.dump();
  }

  /// Build an error reply.
  ///
  /// \param id Request id, or null when it could not be read.
  /// \param code JSON-RPC error code.
  /// \param message Error description.
  /// \return Reply line.
  std::string
  error_reply(const Json &id, int code, std::string_view message)
  {
    return Json {
      Json::Object {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", Json::Object {{"code", code}, {"message", message}}},
      }
    }.dump();
  }

  /// Answer `initialize`: agree on a protocol revision and announce the tools.
  ///
  /// \param params Request parameters.
  /// \return Result object.
  Json
  initialize(const Json &params)
  {
    const std::string_view requested = params.string_or_empty("protocolVersion");
    const bool known = std::ranges::find(kProtocolVersions, requested) != kProtocolVersions.end();
    return Json::Object {
      {"protocolVersion", known ? requested : kProtocolVersions.back()},
      {"capabilities", Json::Object {{"tools", Json::Object {{"listChanged", false}}}}},
      {"serverInfo", Json::Object {{"name", "comprendo"}, {"version", COMPRENDO_VERSION}}},
      {"instructions", kInstructions},
    };
  }

  /// Answer `tools/list`.
  ///
  /// \param tools Chart tools.
  /// \return Result object.
  Json
  list_tools(const Comprendo::ChartTools &tools)
  {
    Json::Array listed;
    for (const Comprendo::ToolInfo &tool : tools.tools())
    {
      listed.emplace_back(
        Json::Object {
          {"name", tool.name},
          {"description", tool.description},
          {"inputSchema", tool.input_schema},
        }
      );
    }
    return Json::Object {{"tools", std::move(listed)}};
  }

  /// Wrap tool output as a `tools/call` result.
  ///
  /// \param text Output or error text.
  /// \param is_error True when the tool failed.
  /// \return Result object.
  Json
  tool_result(std::string text, bool is_error)
  {
    return Json::Object {
      {"content", Json::Array {Json::Object {{"type", "text"}, {"text", std::move(text)}}}},
      {"isError", is_error},
    };
  }
} // namespace

namespace Comprendo
{
  std::optional<std::string>
  handle_mcp_message(std::string_view line, const ChartTools &tools)
  {
    const auto message = Json::parse(line);
    if (!message)
    {
      return error_reply(nullptr, kParseError, message.error());
    }
    if (message->as_object() == nullptr)
    {
      return error_reply(nullptr, kInvalidRequest, "expected a JSON-RPC request object");
    }
    const Json *id = message->find("id");
    const std::string_view method = message->string_or_empty("method");
    if (id == nullptr)
    {
      return std::nullopt;
    }
    if (method.empty())
    {
      return error_reply(*id, kInvalidRequest, "missing method");
    }
    const Json *found_params = message->find("params");
    const Json params = found_params != nullptr ? *found_params : Json {Json::Object {}};
    if (method == "initialize")
    {
      return reply(*id, initialize(params));
    }
    if (method == "ping")
    {
      return reply(*id, Json::Object {});
    }
    if (method == "tools/list")
    {
      return reply(*id, list_tools(tools));
    }
    if (method != "tools/call")
    {
      return error_reply(
        *id, kMethodNotFound, std::string {"unknown method "} + std::string {method}
      );
    }
    const std::string_view name = params.string_or_empty("name");
    const bool known
      = std::ranges::any_of(tools.tools(), [&](const ToolInfo &tool) { return tool.name == name; });
    if (!known)
    {
      return error_reply(*id, kInvalidParams, std::string {"unknown tool "} + std::string {name});
    }
    const Json *arguments = params.find("arguments");
    const auto output = tools.call(name, arguments != nullptr ? *arguments : Json {});
    if (!output)
    {
      Logger::info("{}: {}", name, output.error());
    }
    return reply(*id, output ? tool_result(*output, false) : tool_result(output.error(), true));
  }

  void
  serve_mcp(std::istream &in, std::ostream &out, const ChartTools &tools)
  {
    std::string line;
    while (std::getline(in, line))
    {
      if (line.ends_with('\r'))
      {
        line.pop_back();
      }
      if (line.find_first_not_of(" \t") == std::string::npos)
      {
        continue;
      }
      if (const auto answer = handle_mcp_message(line, tools))
      {
        out << *answer << '\n';
        out.flush();
      }
    }
  }
} // namespace Comprendo
