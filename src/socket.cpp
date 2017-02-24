#include "socket.h"

#include <errno.h>
#include <mutex>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>

namespace shk {
namespace {

// Johan told me to pick 20, because it's a prime number
const int SOCKET_BACKLOG_SIZE = 20;

class UnixSocket : public Socket {
 public:
  UnixSocket(int socket)
      : socket_(socket) {}

  UnixSocket(const UnixSocket &) = delete;
  UnixSocket &operator=(const UnixSocket &) = delete;

  virtual ~UnixSocket() {
    close(socket_);
  }

  virtual size_t recv(
      uint8_t *buffer,
      size_t length) throw(std::runtime_error) override {
    auto result = ::recv(socket_, buffer, length, 0);
    if (result >= 0) {
      return static_cast<size_t>(result);
    } else {
      throw std::runtime_error(strerror(errno));
    }
  }

  virtual void send(
      const uint8_t *buffer,
      size_t length) throw(std::runtime_error) override {
    auto result = ::send(socket_, buffer, length, 0);
    if (static_cast<size_t>(result) == length) {
    } else if (result < 0) {
      throw std::runtime_error(strerror(errno));
    } else {
      // When does this happen? Does it ever happen on blocking writes?
      throw std::runtime_error("Incomplete write");
    }
  }

 private:
  const int socket_;
};

template<typename Callback>
std::shared_ptr<void> finally(const Callback &callback) {
  return std::shared_ptr<void>(nullptr, [callback](void *) {
    callback();
  });
}

class UnixServer : public Server {
 public:
  UnixServer(const std::string &path, const HandleSocket &handle)
      : path_(path),
        handle_(handle) {}

  virtual ServeError run() throw(std::runtime_error) override {
    if (done_) {
      throw std::runtime_error("run() called when already called");
    }

    const auto cleanup = finally([this] {
      std::unique_lock<std::mutex> lock(mutex_);
      done_ = true;
      condition_.notify_all();
    });

    int sock;
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
      throw std::runtime_error(strerror(errno));
    }

    struct sockaddr_un local;
    local.sun_family = AF_UNIX;
    strncpy(local.sun_path, path_.c_str(), sizeof(local.sun_path));
    socklen_t len = strlen(local.sun_path) + sizeof(local.sun_family) + 1;
    if (bind(sock, (struct sockaddr *)&local, len) == -1) {
      if (errno == EINVAL) {
        return ServeError::PATH_IN_USE;
      } else {
        throw std::runtime_error(strerror(errno));
      }
    }

    if (listen(sock, SOCKET_BACKLOG_SIZE) == -1) {
      throw std::runtime_error(strerror(errno));
    }

    {
      std::unique_lock<std::mutex> lock(mutex_);
      socket_ = sock;
      condition_.notify_all();
    }

    for (;;) {
      struct sockaddr_un remote;
      int inner_sock;
      socklen_t remote_size = sizeof(remote);
      if ((inner_sock = accept(sock, (struct sockaddr *)&remote, &remote_size)) == -1) {
        if (errno == EINTR) {
          // Syscall was interrupted. Retry.
        } else if (errno == ECONNABORTED || errno == EBADF) {
          return ServeError::SUCCESS;
        } else {
          throw std::runtime_error(strerror(errno));
        }
      }

      handle_(std::unique_ptr<Socket>(new UnixSocket(inner_sock)));
    }

    return ServeError::SUCCESS;
  }

  virtual void wait() override {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] { return socket_ != 0 || done_; });
  }

  virtual void close() override {
    wait();
    if (socket_) {
      ::close(socket_);
    }
    unlink(path_.c_str());
  }

 private:
  const std::string path_;
  const HandleSocket handle_;
  std::mutex mutex_;
  std::condition_variable condition_;
  int socket_ = 0;  // Protected by mutex_
  bool done_ = false;  // Protected by mutex_
};

class DummySocket : public Socket {
 public:
  virtual size_t recv(
      uint8_t *buffer, size_t length) throw(std::runtime_error) override {
    throw std::runtime_error("DummySocket can't receive data");
    return 0;
  }

  virtual void send(
      const uint8_t *buffer, size_t length) throw(std::runtime_error) override {
    throw std::runtime_error("DummySocket can't send data");
  }
};

}  // anonymous namespace

std::unique_ptr<Server> serve(
    const std::string &path,
    const HandleSocket &handle) {
  return std::unique_ptr<Server>(new UnixServer(path, handle));
}

std::unique_ptr<Socket> connect(const std::string &path) throw(std::runtime_error) {
  int sock;

  if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    throw std::runtime_error(strerror(errno));
  }

  struct sockaddr_un remote;
  remote.sun_family = AF_UNIX;
  strncpy(remote.sun_path, path.c_str(), sizeof(remote.sun_path));
  socklen_t len = strlen(remote.sun_path) + sizeof(remote.sun_family) + 1;
  if (connect(sock, (struct sockaddr *)&remote, len) == -1) {
    throw std::runtime_error(strerror(errno));
  }

  return std::unique_ptr<Socket>(new UnixSocket(sock));
}

std::unique_ptr<Socket> dummySocket() {
  return std::unique_ptr<Socket>(new DummySocket());
}

}  // namespace shk
