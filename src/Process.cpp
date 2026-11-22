/// \file
/// \brief Running external tools with posix_spawn and capturing their output.

#include "Comprendo/Process.hpp"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

extern char **environ;

namespace
{
  /// Exit-code offset used for children terminated by a signal, as shells do.
  constexpr int kSignalExitBase = 128;

  /// An owned file descriptor closed on destruction.
  class Fd
  {
  public:
    /// Wrap \p fd.
    ///
    /// \param fd Descriptor to own, or -1.
    explicit Fd(int fd = -1) : m_fd(fd) { }

    /// Close the descriptor.
    ~Fd() { reset(); }

    /// Take over another descriptor.
    ///
    /// \param other Descriptor to move from; left empty.
    Fd(Fd &&other) noexcept : m_fd(std::exchange(other.m_fd, -1)) { }

    /// Take over another descriptor, closing the current one.
    ///
    /// \param other Descriptor to move from; left empty.
    /// \return This descriptor.
    Fd &
    operator=(Fd &&other) noexcept
    {
      reset(std::exchange(other.m_fd, -1));
      return *this;
    }

    Fd(const Fd &) = delete;
    Fd &
    operator=(const Fd &) = delete;

    /// Access the raw descriptor.
    ///
    /// \return Descriptor, or -1 when empty.
    [[nodiscard]]
    int
    get() const
    {
      return m_fd;
    }

    /// Close the current descriptor and own \p fd instead.
    ///
    /// \param fd Replacement descriptor, or -1.
    void
    reset(int fd = -1)
    {
      if (m_fd >= 0)
      {
        ::close(m_fd);
      }
      m_fd = fd;
    }

  private:
    /// Owned descriptor, or -1.
    int m_fd;
  };

  /// The two ends of a pipe.
  struct Pipe
  {
    /// Read end.
    Fd read;
    /// Write end.
    Fd write;
  };

  /// Create a close-on-exec pipe.
  ///
  /// \return The pipe, or an error message.
  std::expected<Pipe, std::string>
  make_pipe()
  {
    std::array<int, 2> fds {};
    if (::pipe2(fds.data(), O_CLOEXEC) != 0)
    {
      return std::unexpected(fmt::format("pipe failed: {}", std::strerror(errno)));
    }
    return Pipe {.read = Fd {fds[0]}, .write = Fd {fds[1]}};
  }

  /// RAII owner of posix_spawn file actions.
  class FileActions
  {
  public:
    /// Initialize empty file actions.
    FileActions() { posix_spawn_file_actions_init(&m_actions); }

    /// Destroy the file actions.
    ~FileActions() { posix_spawn_file_actions_destroy(&m_actions); }

    FileActions(const FileActions &) = delete;
    FileActions &
    operator=(const FileActions &) = delete;
    FileActions(FileActions &&) = delete;
    FileActions &
    operator=(FileActions &&) = delete;

    /// Access the underlying actions.
    ///
    /// \return Pointer usable with posix_spawn.
    posix_spawn_file_actions_t *
    get()
    {
      return &m_actions;
    }

  private:
    /// Underlying actions.
    posix_spawn_file_actions_t m_actions {};
  };

  /// Read both pipes until the child closes them, without risking a full-pipe deadlock.
  ///
  /// \param out Read end of the child's standard output.
  /// \param err Read end of the child's standard error.
  /// \param result Receives the captured text.
  /// \return Nothing, or an error message.
  std::expected<void, std::string>
  drain(Fd &out, Fd &err, Comprendo::ProcessResult &result)
  {
    constexpr std::size_t kChunk = 65536;
    std::vector<char> buffer(kChunk);
    std::array<pollfd, 2> fds {
      pollfd {.fd = out.get(), .events = POLLIN, .revents = 0},
      pollfd {.fd = err.get(), .events = POLLIN, .revents = 0},
    };
    std::array<std::string *, 2> sinks {&result.out, &result.err};
    while (fds[0].fd >= 0 || fds[1].fd >= 0)
    {
      if (::poll(fds.data(), fds.size(), -1) < 0)
      {
        if (errno == EINTR)
        {
          continue;
        }
        return std::unexpected(fmt::format("poll failed: {}", std::strerror(errno)));
      }
      for (std::size_t i = 0; i < fds.size(); ++i)
      {
        if (fds[i].fd < 0 || fds[i].revents == 0)
        {
          continue;
        }
        const ssize_t count = ::read(fds[i].fd, buffer.data(), buffer.size());
        if (count > 0)
        {
          sinks[i]->append(buffer.data(), static_cast<std::size_t>(count));
        }
        else if (count == 0 || errno != EINTR)
        {
          fds[i].fd = -1;
        }
      }
    }
    return {};
  }
} // namespace

namespace Comprendo
{
  std::expected<ProcessResult, std::string>
  run_process(const std::vector<std::string> &argv, const std::filesystem::path &cwd)
  {
    if (argv.empty())
    {
      return std::unexpected("cannot run an empty command");
    }
    auto out = make_pipe();
    auto err = make_pipe();
    if (!out || !err)
    {
      return std::unexpected(out ? err.error() : out.error());
    }

    FileActions actions;
    posix_spawn_file_actions_addopen(actions.get(), STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(actions.get(), out->write.get(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(actions.get(), err->write.get(), STDERR_FILENO);
    if (!cwd.empty())
    {
      posix_spawn_file_actions_addchdir_np(actions.get(), cwd.c_str());
    }

    std::vector<char *> args;
    args.reserve(argv.size() + 1);
    for (const std::string &arg : argv)
    {
      args.push_back(const_cast<char *>(arg.c_str()));
    }
    args.push_back(nullptr);

    pid_t pid = 0;
    const int spawned = posix_spawnp(&pid, args[0], actions.get(), nullptr, args.data(), environ);
    out->write.reset();
    err->write.reset();
    if (spawned != 0)
    {
      return std::unexpected(fmt::format("cannot run '{}': {}", argv[0], std::strerror(spawned)));
    }

    ProcessResult result;
    auto drained = drain(out->read, err->read, result);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0)
    {
      if (errno != EINTR)
      {
        return std::unexpected(fmt::format("waitpid failed: {}", std::strerror(errno)));
      }
    }
    if (!drained)
    {
      return std::unexpected(drained.error());
    }
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : kSignalExitBase + WTERMSIG(status);
    return result;
  }
} // namespace Comprendo
