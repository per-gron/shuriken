#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace shk {

/**
 * Socket is an abstraction for a bidirectional stream of bytes. It supports
 * only blocking I/O.
 *
 * Objects of this class can be used from any thread.
 */
class Socket {
 public:
  virtual ~Socket() = default;

  /**
   * On failure, err will be set to a non-empty string.
   */
  virtual size_t recv(uint8_t *buffer, size_t length) throw(std::runtime_error) = 0;

  /**
   * On failure, err will be set to a non-empty string.
   */
  virtual void send(const uint8_t *buffer, size_t length) throw(std::runtime_error) = 0;
};

enum class ServeError {
  SUCCESS,
  PATH_IN_USE
};

class Server {
 public:
  virtual ~Server() = default;

  /**
   * Can only be called once per Server object.
   */
  virtual ServeError run() throw(std::runtime_error) = 0;

  /**
   * Wait for the server to be ready for accepting connections.
   *
   * Can be called from any thread while the server is running.
   */
  virtual void wait() = 0;

  /**
   * Can be called from any thread while the server is running.
   */
  virtual void close() = 0;
};

/**
 * Handle a client connection. If the operation is expected to take a while, it
 * is preferable to move the computation to another thread and return early
 * from this callback, since the server is not able to accept other connections
 * while a HandleSocket callback is running.
 */
using HandleSocket = std::function<void (std::unique_ptr<Socket> &&socket)>;

/**
 * The server part of this primitive socket library. This function attempts to
 * bind a file socket with the specified path and will start accepting
 * connections. It sets up a single-threaded blocking server, so it's not
 * designed for many client connections.
 */
std::unique_ptr<Server> serve(
    const std::string &path,
    const HandleSocket &handle);

/**
 * The client part of this primitive socket library.
 */
std::unique_ptr<Socket> connect(const std::string &path) throw(std::runtime_error);

/**
 * Create a Socket object that always fails.
 */
std::unique_ptr<Socket> dummySocket();

}  // namespace shk
