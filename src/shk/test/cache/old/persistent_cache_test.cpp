// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "persistent_cache.h"

#include <thread>

#include "test.h"

namespace {

class RecordingPersistentCache : public PersistentCache {
 public:
  RecordingPersistentCache(
      std::vector<UploadRequest> &upload_requests,
      std::vector<LookupRequest> &lookup_requests,
      const std::string &error)
      : upload_requests_(upload_requests),
        lookup_requests_(lookup_requests),
        error_(error) {}

  virtual void upload(
      const UploadRequest &upload_request) throw(ShurikenError) override {
    if (!error_.empty()) {
      throw ShurikenError(error_);
    } else {
      upload_requests_.push_back(upload_request);
    }
  }

  virtual LookupResponse lookup(
      const LookupRequest &lookup_request) throw(ShurikenError) override {
    if (!error_.empty()) {
      throw ShurikenError(error_);
    } else {
      lookup_requests_.push_back(lookup_request);
      LookupResponse response;
      response["path"] = CacheEntry();
      return response;
    }
  }

 private:
  std::vector<UploadRequest> &upload_requests_;
  std::vector<LookupRequest> &lookup_requests_;
  const std::string error_;
};

const char kTestFilename[] = "persistentcache-tempfile";

struct PersistentCacheTest : public testing::Test {
  virtual void SetUp() {
    // In case a crashing test left a stale file behind.
    unlink(kTestFilename);
  }
  virtual void TearDown() {
    unlink(kTestFilename);
  }
};

template<typename Callback>
void setupClient(
    std::vector<UploadRequest> &upload_requests,
    std::vector<LookupRequest> &lookup_requests,
    Callback &&callback,
    const std::string &error = "") {
  std::string err;

  const auto handle_socket = persistentCacheServer(
      std::unique_ptr<RecordingPersistentCache>(
          new RecordingPersistentCache(
              upload_requests, lookup_requests, error)));
  const std::shared_ptr<Server> server = serve(kTestFilename, handle_socket);

  std::thread server_thread([server] {
    server->run();
  });

  // Ensure the server has had time to actually open the file socket before
  // we attempt to connect to it.
  server->wait();

  const auto client_cache = persistentCacheClient(
      [&]() {
        return connect(kTestFilename);
      });

  callback(*client_cache);

  server->close();
  server_thread.join();
}

TEST_F(PersistentCacheTest, Upload) {
  std::vector<UploadRequest> upload_requests;
  std::vector<LookupRequest> lookup_requests;
  std::string err;

  setupClient(
      upload_requests,
      lookup_requests,
      [&](PersistentCache &cache) {
        UploadRequest request;
        request.files.push_back({ SHA1::Digest(), "a/path" });

        cache.upload(request);

        EXPECT_TRUE(lookup_requests.empty());
        EXPECT_EQ(upload_requests.size(), 1);
        EXPECT_EQ(upload_requests[0].files[0].path, "a/path");
      });
}

TEST_F(PersistentCacheTest, UploadError) {
  std::vector<UploadRequest> upload_requests;
  std::vector<LookupRequest> lookup_requests;

  setupClient(
      upload_requests,
      lookup_requests,
      [&](PersistentCache &cache) {
        UploadRequest request;
        request.files.push_back({ SHA1::Digest(), "a/path" });

        try {
          cache.upload(request);
          EXPECT_TRUE(false);
        } catch (ShurikenError &error) {
          EXPECT_EQ(std::string(error.what()), "an_error");
        }
      },
      "an_error");
}

TEST_F(PersistentCacheTest, Lookup) {
  std::vector<UploadRequest> upload_requests;
  std::vector<LookupRequest> lookup_requests;

  setupClient(
      upload_requests,
      lookup_requests,
      [&](PersistentCache &cache) {
        LookupRequest request;
        request.keys["a/path"] = SHA1::Digest();

        const auto response = cache.lookup(request);

        EXPECT_TRUE(upload_requests.empty());

        EXPECT_EQ(lookup_requests.size(), 1);
        EXPECT_EQ(lookup_requests[0].keys.count("a/path"), 1);

        EXPECT_EQ(response.count("path"), 1);
      });
}

TEST_F(PersistentCacheTest, LookupError) {
  std::vector<UploadRequest> upload_requests;
  std::vector<LookupRequest> lookup_requests;
  std::string err;

  setupClient(
      upload_requests,
      lookup_requests,
      [&](PersistentCache &cache) {
        LookupRequest request;
        request.keys["a/path"] = SHA1::Digest();

        try {
          cache.lookup(request);
          EXPECT_TRUE(false);
        } catch (ShurikenError &error) {
          EXPECT_EQ(std::string(error.what()), "an_error");
        }
      },
      "an_error");
}

}  // anonymous namespace
