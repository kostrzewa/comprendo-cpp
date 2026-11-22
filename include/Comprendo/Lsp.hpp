#pragma once

/// \file
/// \brief Minimal Language Server Protocol client: framing, URIs, and requests over stdio.

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Comprendo/Json.hpp"
#include "Comprendo/Process.hpp"

namespace Comprendo
{
  /// Frame a JSON-RPC message with the LSP `Content-Length` header.
  ///
  /// \param message Message to send.
  /// \return Header and compact JSON body.
  [[nodiscard]]
  std::string
  encode_lsp_message(const Json &message);

  /// Splits a byte stream into LSP messages.
  class LspMessageReader
  {
  public:
    /// Append received bytes.
    ///
    /// \param bytes Bytes read from the server.
    void
    feed(std::string_view bytes);

    /// Take the next complete message.
    ///
    /// \return The message, nothing when more bytes are needed, or an error for a
    ///         malformed header or body.
    [[nodiscard]]
    std::expected<std::optional<Json>, std::string>
    next();

  private:
    /// Bytes received but not yet consumed.
    std::string m_buffer;
  };

  /// Convert an absolute path to a `file://` URI, percent-encoding reserved bytes.
  ///
  /// \param path Absolute path.
  /// \return URI.
  [[nodiscard]]
  std::string
  path_to_uri(const std::filesystem::path &path);

  /// Convert a `file://` URI back to a path.
  ///
  /// \param uri URI.
  /// \return Path, or nothing for other schemes and malformed escapes.
  [[nodiscard]]
  std::optional<std::filesystem::path>
  uri_to_path(std::string_view uri);

  /// A language server running as a child process, spoken to over stdio.
  ///
  /// Requests are synchronous. While waiting for a response, server requests
  /// (such as progress or capability registration) are answered with a null
  /// result and notifications are skipped.
  class LspClient
  {
  public:
    /// Start a language server.
    ///
    /// \param argv Server command line.
    /// \param cwd Working directory of the server.
    /// \param timeout Longest wait for any single response.
    /// \return The client, or an error message.
    [[nodiscard]]
    static std::expected<LspClient, std::string>
    start(
      const std::vector<std::string> &argv,
      const std::filesystem::path &cwd,
      std::chrono::milliseconds timeout
    );

    /// Send a request and wait for its result.
    ///
    /// \param method Method name.
    /// \param params Parameters.
    /// \return The result member, or an error message (including server errors).
    [[nodiscard]]
    std::expected<Json, std::string>
    request(std::string_view method, Json params);

    /// Send a notification.
    ///
    /// \param method Method name.
    /// \param params Parameters.
    /// \return Nothing, or an error message.
    [[nodiscard]]
    std::expected<void, std::string>
    notify(std::string_view method, Json params);

    /// Run the `shutdown` request and `exit` notification, then wait for the server.
    ///
    /// \return Nothing, or an error message from the shutdown request.
    [[nodiscard]]
    std::expected<void, std::string>
    shutdown();

  private:
    /// Wrap a started server.
    ///
    /// \param process Server process.
    /// \param timeout Longest wait for any single response.
    LspClient(Subprocess process, std::chrono::milliseconds timeout) :
      m_process(std::move(process)), m_timeout(timeout)
    {
    }

    /// Read the next message from the server.
    ///
    /// \return The message, or an error message on timeout or malformed input.
    [[nodiscard]]
    std::expected<Json, std::string>
    next_message();

    /// Server process.
    Subprocess m_process;
    /// Framing state of the server's output.
    LspMessageReader m_reader;
    /// Longest wait for any single response.
    std::chrono::milliseconds m_timeout;
    /// Id of the next request.
    std::int64_t m_next_id = 1;
  };
} // namespace Comprendo
