/// \file
/// \brief Minimal Language Server Protocol client: framing, URIs, and requests over stdio.

#include "Comprendo/Lsp.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace
{
  /// Separator between LSP headers and body.
  constexpr std::string_view kHeaderEnd = "\r\n\r\n";

  /// Header announcing the body length, compared case-insensitively.
  constexpr std::string_view kContentLength = "content-length:";

  /// Scheme prefix of file URIs.
  constexpr std::string_view kFileScheme = "file://";

  /// Grace period for the server to exit after `exit`.
  constexpr std::chrono::milliseconds kExitGrace {5000};

  /// Check whether a byte may appear unescaped in a file URI path.
  ///
  /// \param c Byte.
  /// \return True for RFC 3986 unreserved characters and '/'.
  bool
  is_uri_safe(unsigned char c)
  {
    return std::isalnum(c) != 0 || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
  }

  /// Read the value of the Content-Length header.
  ///
  /// \param headers Header block without the terminating blank line.
  /// \return Body length, or nothing when absent or malformed.
  std::optional<std::size_t>
  content_length(std::string_view headers)
  {
    std::size_t start = 0;
    while (start < headers.size())
    {
      std::size_t end = headers.find("\r\n", start);
      if (end == std::string_view::npos)
      {
        end = headers.size();
      }
      std::string line {headers.substr(start, end - start)};
      std::ranges::transform(line, line.begin(), [](unsigned char c) { return std::tolower(c); });
      if (line.starts_with(kContentLength))
      {
        std::string_view value = std::string_view {line}.substr(kContentLength.size());
        value.remove_prefix(std::min(value.find_first_not_of(' '), value.size()));
        std::size_t length = 0;
        const auto [ptr, error]
          = std::from_chars(value.data(), value.data() + value.size(), length);
        if (error == std::errc {} && ptr == value.data() + value.size())
        {
          return length;
        }
        return std::nullopt;
      }
      start = end + 2;
    }
    return std::nullopt;
  }
} // namespace

namespace Comprendo
{
  std::string
  encode_lsp_message(const Json &message)
  {
    const std::string body = message.dump();
    return fmt::format("Content-Length: {}\r\n\r\n{}", body.size(), body);
  }

  void
  LspMessageReader::feed(std::string_view bytes)
  {
    m_buffer.append(bytes);
  }

  std::expected<std::optional<Json>, std::string>
  LspMessageReader::next()
  {
    const std::size_t header_end = m_buffer.find(kHeaderEnd);
    if (header_end == std::string::npos)
    {
      return std::optional<Json> {};
    }
    const auto length = content_length(std::string_view {m_buffer}.substr(0, header_end));
    if (!length)
    {
      return std::unexpected("LSP message without a valid Content-Length header");
    }
    const std::size_t body_start = header_end + kHeaderEnd.size();
    if (m_buffer.size() - body_start < *length)
    {
      return std::optional<Json> {};
    }
    auto message = Json::parse(std::string_view {m_buffer}.substr(body_start, *length));
    m_buffer.erase(0, body_start + *length);
    if (!message)
    {
      return std::unexpected(message.error());
    }
    return std::optional<Json> {std::move(*message)};
  }

  std::string
  path_to_uri(const std::filesystem::path &path)
  {
    std::string uri {kFileScheme};
    for (const char c : path.generic_string())
    {
      const auto byte = static_cast<unsigned char>(c);
      if (is_uri_safe(byte))
      {
        uri.push_back(c);
      }
      else
      {
        uri += fmt::format("%{:02X}", byte);
      }
    }
    return uri;
  }

  std::optional<std::filesystem::path>
  uri_to_path(std::string_view uri)
  {
    if (!uri.starts_with(kFileScheme))
    {
      return std::nullopt;
    }
    uri.remove_prefix(kFileScheme.size());
    std::string path;
    for (std::size_t i = 0; i < uri.size(); ++i)
    {
      if (uri[i] != '%')
      {
        path.push_back(uri[i]);
        continue;
      }
      unsigned int byte = 0;
      const char *first = uri.data() + i + 1;
      if (
        i + 2 >= uri.size()
        || std::from_chars(first, first + 2, byte, 16)
          != std::from_chars_result {first + 2, std::errc {}}
      )
      {
        return std::nullopt;
      }
      path.push_back(static_cast<char>(byte));
      i += 2;
    }
    return std::filesystem::path {path};
  }

  std::expected<LspClient, std::string>
  LspClient::start(
    const std::vector<std::string> &argv,
    const std::filesystem::path &cwd,
    std::chrono::milliseconds timeout
  )
  {
    auto process = Subprocess::spawn(argv, cwd);
    if (!process)
    {
      return std::unexpected(process.error());
    }
    return LspClient {std::move(*process), timeout};
  }

  std::expected<Json, std::string>
  LspClient::next_message()
  {
    while (true)
    {
      auto message = m_reader.next();
      if (!message)
      {
        return std::unexpected(message.error());
      }
      if (*message)
      {
        return std::move(**message);
      }
      auto bytes = m_process.read_some(m_timeout);
      if (!bytes)
      {
        return std::unexpected(bytes.error());
      }
      m_reader.feed(*bytes);
    }
  }

  std::expected<Json, std::string>
  LspClient::request(std::string_view method, Json params)
  {
    const std::int64_t id = m_next_id++;
    const Json message {Json::Object {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"method", method},
      {"params", std::move(params)},
    }};
    if (auto sent = m_process.write(encode_lsp_message(message)); !sent)
    {
      return std::unexpected(sent.error());
    }
    while (true)
    {
      auto reply = next_message();
      if (!reply)
      {
        return std::unexpected(fmt::format("{}: {}", method, reply.error()));
      }
      const Json *reply_id = reply->find("id");
      const bool is_request = reply->find("method") != nullptr;
      if (is_request && reply_id != nullptr)
      {
        const Json answer {
          Json::Object {{"jsonrpc", "2.0"}, {"id", *reply_id}, {"result", nullptr}}
        };
        if (auto sent = m_process.write(encode_lsp_message(answer)); !sent)
        {
          return std::unexpected(sent.error());
        }
        continue;
      }
      if (
        is_request || reply_id == nullptr || reply_id->as_number() == nullptr
        || *reply_id->as_number() != static_cast<double>(id)
      )
      {
        continue;
      }
      if (const Json *error = reply->find("error"))
      {
        return std::unexpected(
          fmt::format("{}: {}", method, std::string {error->string_or_empty("message")})
        );
      }
      const Json *result = reply->find("result");
      return result == nullptr ? Json {} : *result;
    }
  }

  std::expected<void, std::string>
  LspClient::notify(std::string_view method, Json params)
  {
    const Json message {Json::Object {
      {"jsonrpc", "2.0"},
      {"method", method},
      {"params", std::move(params)},
    }};
    return m_process.write(encode_lsp_message(message));
  }

  std::expected<void, std::string>
  LspClient::shutdown()
  {
    auto stopped = request("shutdown", Json {});
    // NOLINTNEXTLINE(bugprone-unused-return-value): exit fails only when the server already left.
    static_cast<void>(notify("exit", Json {}));
    m_process.finish(kExitGrace);
    if (!stopped)
    {
      return std::unexpected(stopped.error());
    }
    return {};
  }
} // namespace Comprendo
