#include <catch.hpp>

#include "socket.h"

#include <sys/stat.h>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace shk {

const char kTestFilename[] = "socket-tempfile";

void setupAndClose() {
  const std::shared_ptr<Server> server = serve(kTestFilename, [](std::unique_ptr<Socket> &&) {
    CHECK(false);
  });
  std::thread server_thread([server] {
    CHECK(server->run() == ServeError::SUCCESS);
  });
  server->close();
  server_thread.join();
}

TEST_CASE("Socket") {
  // In case a crashing test left a stale file behind.
  unlink(kTestFilename);

  SECTION("ConnectToMissingFile") {
    try {
      connect("missing-file");
      CHECK(false);
    } catch (std::runtime_error &error) {
      // Expected
    }
  }

  SECTION("SetupAndCloseServer") {
    setupAndClose();
  }

  SECTION("SetupTwoCloseServerTwise") {
    setupAndClose();
    setupAndClose();
  }

  SECTION("SetupTwoAtTheSameTime") {
    // This should fail, but it should still be possible to close

    const std::shared_ptr<Server> server = serve(kTestFilename, [](std::unique_ptr<Socket> &&) {
      CHECK(false);
    });
    std::thread server_thread([server] {
      server->run();
    });
    server->wait();

    const std::shared_ptr<Server> server_2 = serve(kTestFilename, [](std::unique_ptr<Socket> &&) {
      CHECK(false);
    });
    std::thread server_thread_2([server_2] {
      try {
        server_2->run();
        CHECK(false);
      } catch (std::runtime_error &error) {
        // Expected
      }
    });

    server->close();
    server_2->close();
    server_thread.join();
    server_thread_2.join();
  }

  SECTION("RunServerTwice") {
    const std::shared_ptr<Server> server = serve(kTestFilename, [](std::unique_ptr<Socket> &&) {
      CHECK(false);
    });
    std::thread server_thread([server] {
      server->run();
    });

    server->close();
    server_thread.join();

    try {
      server->run();
      CHECK(false);
    } catch (std::runtime_error &error) {
      // Expected
    }
  }

  SECTION("ServerSend") {
    static const uint8_t * const kStr =
        reinterpret_cast<const uint8_t *>("Hey!");
    static const size_t kStrLen = 4;

    const std::shared_ptr<Server> server = serve(kTestFilename, [](
        std::unique_ptr<Socket> &&socket) {
      socket->send(kStr, kStrLen);
    });
    std::thread server_thread([server] {
      CHECK(server->run() == ServeError::SUCCESS);
    });

    // Ensure the server has had time to actually open the file socket before
    // we attempt to connect to it.
    server->wait();

    const auto client = connect(kTestFilename);

    uint8_t buf[128];
    memset(buf, 0, sizeof(buf));
    size_t recv_bytes = 0;
    while (recv_bytes < kStrLen) {
      recv_bytes += client->recv(
          buf + recv_bytes, sizeof(buf) - recv_bytes);
    }

    CHECK(memcmp(buf, kStr, kStrLen) == 0);

    server->close();
    server_thread.join();
  }

  SECTION("ClientSend") {
    static const uint8_t * const kStr =
        reinterpret_cast<const uint8_t *>("Hey!");
    static const size_t kStrLen = 4;

    const std::shared_ptr<Server> server = serve(kTestFilename, [&](
        std::unique_ptr<Socket> &&socket) {
      uint8_t buf[128];
      memset(buf, 0, sizeof(buf));
      size_t recv_bytes = 0;

      while (recv_bytes < kStrLen) {
        recv_bytes += socket->recv(
            buf + recv_bytes, sizeof(buf) - recv_bytes);
      }

      CHECK(recv_bytes == kStrLen);
      CHECK(memcmp(buf, kStr, kStrLen) == 0);

      // It's important to close the server from here and not from the test
      // thread. This avoid the race where the server is closed before the recv
      // call has had time to finish.
      server->close();
    });
    std::thread server_thread([server] {
      CHECK(server->run() == ServeError::SUCCESS);
    });

    // Ensure the server has had time to actually open the file socket before
    // we attempt to connect to it.
    server->wait();

    const auto client = connect(kTestFilename);

    client->send(kStr, kStrLen);

    // Won't finish until the server is closed from the connection handler
    server_thread.join();
  }

  SECTION("DummySocketSend") {
    const auto socket = dummySocket();
    uint8_t buf[16];
    try {
      socket->send(buf, sizeof(buf));
      CHECK(false);
    } catch (std::runtime_error &error) {
      // Expected.
    }
  }

  SECTION("DummySocketRecv") {
    const auto socket = dummySocket();
    uint8_t buf[16];
    try {
      CHECK(socket->recv(buf, sizeof(buf)) == 0);
      CHECK(false);
    } catch (std::runtime_error &error) {
      // Expected.
    }
  }

  unlink(kTestFilename);
}

}  // namespace shk
