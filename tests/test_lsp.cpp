/// \file
/// \brief Tests for the Language Server Protocol client: framing, URIs, and requests.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "Comprendo/Json.hpp"
#include "Comprendo/Lsp.hpp"
#include "Comprendo/Process.hpp"
#include "Checks.hpp"
#include "TempDir.hpp"

using Catch::Matchers::ContainsSubstring;
using Comprendo::Json;
using Comprendo::LspMessageReader;

namespace
{
  /// Take the next message from a reader, flattening errors to nothing.
  ///
  /// \param reader Reader to drain.
  /// \return The message's compact JSON, or nothing when no message is complete.
  std::optional<std::string>
  next_dump(LspMessageReader &reader)
  {
    auto message = reader.next();
    if (!message || !*message)
    {
      return std::nullopt;
    }
    return (*message)->dump();
  }
} // namespace

/// Language servers read exactly Content-Length bytes after a blank line, so the
/// header must count the bytes of the compact body, including multi-byte UTF-8.
/// Encodes a small object holding "ä" (two bytes) and compares the whole frame.
TEST_CASE("lsp messages are framed with their byte length", "[lsp]")
{
  const Json message {Json::Object {{"a", "\xC3\xA4"}}};
  CHECK(Comprendo::encode_lsp_message(message) == "Content-Length: 10\r\n\r\n{\"a\":\"\xC3\xA4\"}");
}

/// Pipes deliver arbitrary chunks, so the reader must reassemble messages split
/// anywhere and split messages that arrive together. Feeds two frames byte by
/// byte, checking nothing is returned before a frame is complete, then feeds two
/// frames in one chunk (one with a Content-Type header and lower-case
/// Content-Length) and checks both come out in order.
TEST_CASE("lsp reader reassembles and splits messages", "[lsp]")
{
  LspMessageReader reader;
  const std::string first = Comprendo::encode_lsp_message(Json {1});
  const std::string second = Comprendo::encode_lsp_message(Json {Json::Array {true}});
  std::size_t complete = 0;
  for (const char c : first + second)
  {
    reader.feed(std::string {c});
    if (const auto message = next_dump(reader))
    {
      ++complete;
      CHECK(*message == (complete == 1 ? "1" : "[true]"));
    }
  }
  CHECK(complete == 2);
  CHECK_FALSE(next_dump(reader).has_value());

  reader.feed(
    "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\ncontent-length: 2\r\n\r\n{}" + first
  );
  CHECK(next_dump(reader) == std::optional<std::string> {"{}"});
  CHECK(next_dump(reader) == std::optional<std::string> {"1"});
}

/// A frame without a usable length cannot be skipped safely, so it must be an
/// error rather than a hang or a misparse. Feeds a frame without Content-Length
/// and one with a non-numeric length and checks both are reported as errors.
TEST_CASE("lsp reader rejects frames without a length", "[lsp]")
{
  LspMessageReader missing;
  missing.feed("Content-Type: x\r\n\r\n{}");
  CHECK_THAT(Comprendo::Testing::error_of(missing.next()), ContainsSubstring("Content-Length"));

  LspMessageReader malformed;
  malformed.feed("Content-Length: 2x\r\n\r\n{}");
  CHECK_FALSE(malformed.next().has_value());
}

/// clangd reports locations as file URIs; mapping them back to chart paths must
/// survive spaces and non-ASCII names. Converts such a path to a URI, checks the
/// escapes, converts back and compares, then checks other schemes and broken
/// escapes are rejected.
TEST_CASE("lsp file uris round-trip", "[lsp]")
{
  const std::filesystem::path path {"/home/user/my project/gr\xC3\xBC\xC3\x9F"
                                    "e.cpp"};
  const std::string uri = Comprendo::path_to_uri(path);
  CHECK(uri == "file:///home/user/my%20project/gr%C3%BC%C3%9Fe.cpp");
  CHECK(Comprendo::uri_to_path(uri) == std::optional {path});
  CHECK_FALSE(Comprendo::uri_to_path("https://example.com/a.cpp").has_value());
  CHECK_FALSE(Comprendo::uri_to_path("file:///a%2").has_value());
  CHECK_FALSE(Comprendo::uri_to_path("file:///a%zz").has_value());
}

/// The clangd backend talks to a long-running child through both pipes, which
/// run_process() cannot do. Starts `cat`, writes a line, reads until it is echoed
/// back, checks a read with nothing pending times out, and checks the exit
/// status after closing its input.
TEST_CASE("subprocess talks to a child over pipes", "[lsp][process]")
{
  auto process = Comprendo::Subprocess::spawn({"cat"});
  CHECK(process.has_value());
  if (!process)
  {
    return;
  }
  CHECK(process->write("hello\n").has_value());
  std::string echoed;
  while (echoed.size() < 6)
  {
    auto bytes = process->read_some(std::chrono::seconds {5});
    CHECK(bytes.has_value());
    if (!bytes)
    {
      break;
    }
    echoed += *bytes;
  }
  CHECK(echoed == "hello\n");
  CHECK_FALSE(process->read_some(std::chrono::milliseconds {50}).has_value());
  CHECK(process->finish(std::chrono::seconds {5}) == 0);
}

/// While waiting for a response, servers send notifications, requests of their
/// own, and sometimes stale responses; the client must step over all of them and
/// surface errors. A shell script replays a canned stream (notification, server
/// request, response with a foreign id, result for id 1, error for id 2), then
/// the test checks both requests and that a third one times out.
TEST_CASE("lsp client matches responses to requests", "[lsp]")
{
  const Comprendo::Testing::TempDir dir {"lsp-client"};
  {
    std::ofstream script {dir / "replies", std::ios::binary};
    script << Comprendo::encode_lsp_message(
      Json {Json::Object {{"jsonrpc", "2.0"}, {"method", "window/logMessage"}}}
    );
    script << Comprendo::encode_lsp_message(
      Json {Json::Object {
        {"jsonrpc", "2.0"}, {"id", "s1"}, {"method", "window/workDoneProgress/create"}
      }}
    );
    script << Comprendo::encode_lsp_message(
      Json {Json::Object {{"jsonrpc", "2.0"}, {"id", 7}, {"result", 0}}}
    );
    script << Comprendo::encode_lsp_message(
      Json {Json::Object {{"jsonrpc", "2.0"}, {"id", 1}, {"result", Json::Array {1, 2}}}}
    );
    script << Comprendo::encode_lsp_message(
      Json {Json::Object {
        {"jsonrpc", "2.0"}, {"id", 2}, {"error", Json::Object {{"code", -1}, {"message", "boom"}}}
      }}
    );
  }
  auto client = Comprendo::LspClient::start(
    {"sh", "-c", "cat replies; cat > /dev/null"}, dir.path(), std::chrono::milliseconds {500}
  );
  CHECK(client.has_value());
  if (!client)
  {
    return;
  }
  const auto first = client->request("first", Json {});
  CHECK(first.has_value());
  if (first)
  {
    CHECK(first->dump() == "[1,2]");
  }
  CHECK_THAT(
    Comprendo::Testing::error_of(client->request("second", Json {})), ContainsSubstring("boom")
  );
  CHECK_THAT(
    Comprendo::Testing::error_of(client->request("third", Json {})), ContainsSubstring("third")
  );
}
