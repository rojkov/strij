#include "nodeagent/extensions/task_handlers/openworkflow/artifact_loader.hh"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace strij::nodeagent::task_handlers::openworkflow {
namespace {

constexpr off_t kMaxArtifactBytes = 4 * 1024 * 1024;

auto internalError(std::string_view path, std::string_view operation, int error) -> absl::Status {
  return absl::InternalError(absl::StrCat("failed to ", operation, " workflow artifact '", path,
                                          "': ", std::strerror(error)));
}

} // namespace

auto LoadWorkflowArtifact(std::string_view path) -> absl::StatusOr<std::string> {
  const std::string path_string{path};
  // O_NONBLOCK keeps a FIFO open from blocking the event-loop thread before
  // fstat can reject it. It is ignored for regular files.
  const int fd = ::open(path_string.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) {
    const int error = errno;
    if (error == ENOENT || error == ENOTDIR) {
      return absl::NotFoundError(absl::StrCat("workflow artifact '", path, "' does not exist"));
    }

    return internalError(path, "open", error);
  }

  struct stat status{};
  if (::fstat(fd, &status) != 0) {
    const int error = errno;
    ::close(fd);
    return internalError(path, "stat", error);
  }

  if (!S_ISREG(status.st_mode)) {
    ::close(fd);
    return absl::InvalidArgumentError(
        absl::StrCat("workflow artifact '", path, "' is not a regular file"));
  }

  if (status.st_size < 0 || status.st_size > kMaxArtifactBytes) {
    ::close(fd);
    return absl::InvalidArgumentError(absl::StrCat("workflow artifact '", path, "' exceeds the ",
                                                   kMaxArtifactBytes, "-byte limit"));
  }

  std::string content(static_cast<std::size_t>(status.st_size), '\0');
  std::size_t offset = 0;
  while (offset < content.size()) {
    const ssize_t bytes_read = ::read(fd, content.data() + offset, content.size() - offset);
    if (bytes_read < 0) {
      if (errno == EINTR) {
        continue;
      }

      const int error = errno;
      ::close(fd);
      return internalError(path, "read", error);
    }

    if (bytes_read == 0) {
      break;
    }

    offset += static_cast<std::size_t>(bytes_read);
  }

  content.resize(offset);
  ::close(fd);

  return content;
}

} // namespace strij::nodeagent::task_handlers::openworkflow
