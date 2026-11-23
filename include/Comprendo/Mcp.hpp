#pragma once

/// \file
/// \brief A Model Context Protocol server offering the chart tools over stdio.

#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include "Comprendo/ChartTools.hpp"

namespace Comprendo
{
  /// Handle one JSON-RPC message of the MCP stdio transport.
  ///
  /// Supports `initialize`, `ping`, `tools/list`, and `tools/call`; tool
  /// failures are results with `isError` so the model can correct itself, and
  /// protocol mistakes are JSON-RPC errors. Notifications get no reply.
  ///
  /// \param line One message without its newline.
  /// \param tools Chart tools to offer.
  /// \return The reply line, or nothing for a notification.
  [[nodiscard]]
  std::optional<std::string>
  handle_mcp_message(std::string_view line, const ChartTools &tools);

  /// Serve the chart tools until the input ends: one JSON-RPC message per
  /// line in, one reply per line out (MCP's stdio transport).
  ///
  /// \param in Messages from the client.
  /// \param out Replies to the client; flushed after every reply.
  /// \param tools Chart tools to offer.
  void
  serve_mcp(std::istream &in, std::ostream &out, const ChartTools &tools);
} // namespace Comprendo
