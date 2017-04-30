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

#include <catch.hpp>

#include "file_descriptor_memo.h"

namespace shk {

TEST_CASE("FileDescriptorMemo") {
  FileDescriptorMemo memo;

  SECTION("Open") {
    memo.open(1, 2, "path", false);
    CHECK(memo.getFileDescriptorPath(1, 2) == "path");
  }

  SECTION("GetUnknownPathFromKnownPid") {
    memo.open(1, 2, "path", false);
    CHECK(memo.getFileDescriptorPath(1, 3) == "");
  }

  SECTION("GetPathFromWrongPid") {
    memo.open(1, 2, "path", false);
    CHECK(memo.getFileDescriptorPath(2, 2) == "");
  }

  SECTION("OpenTwice") {
    memo.open(1, 2, "path", false);
    memo.open(1, 2, "new_path", false);
    CHECK(memo.getFileDescriptorPath(1, 2) == "new_path");
  }

  SECTION("OpenTwoInOneProcess") {
    memo.open(1, 2, "path", false);
    memo.open(1, 3, "new_path", false);
    CHECK(memo.getFileDescriptorPath(1, 2) == "path");
    CHECK(memo.getFileDescriptorPath(1, 3) == "new_path");
  }

  SECTION("Close") {
    memo.open(1, 2, "path", false);
    memo.close(1, 2);
    CHECK(memo.getFileDescriptorPath(1, 2) == "");
  }

  SECTION("CloseUnknown") {
    memo.close(1, 2);
  }

  SECTION("Dup") {
    SECTION("Dup") {
      memo.open(1, 2, "path", false);
      memo.dup(1, 2, 3, false);
      CHECK(memo.getFileDescriptorPath(1, 3) == "path");
    }

    SECTION("DupAndClose") {
      memo.open(1, 2, "path", false);
      memo.dup(1, 2, 3, false);
      memo.close(1, 2);
      CHECK(memo.getFileDescriptorPath(1, 3) == "path");
    }

    SECTION("DupAndCloseDup") {
      memo.open(1, 2, "path", false);
      memo.dup(1, 2, 3, false);
      memo.close(1, 3);
      CHECK(memo.getFileDescriptorPath(1, 2) == "path");
    }

    SECTION("DupUnknownPid") {
      memo.dup(1, 2, 3, false);
    }

    SECTION("DupUnknownFd") {
      memo.open(1, 4, "path", false);
      memo.dup(1, 2, 3, false);
    }

    SECTION("DupCloexecOff") {
      memo.open(1, 2, "path", false);
      memo.dup(1, 2, 3, false);
      memo.exec(1);
      CHECK(memo.getFileDescriptorPath(1, 2) == "path");
      CHECK(memo.getFileDescriptorPath(1, 3) == "path");
    }

    SECTION("DupCloexecOn") {
      memo.open(1, 2, "path", false);
      memo.dup(1, 2, 3, true);
      memo.exec(1);
      CHECK(memo.getFileDescriptorPath(1, 2) == "path");
      CHECK(memo.getFileDescriptorPath(1, 3) == "");
    }
  }

  SECTION("Exec") {
    SECTION("Unknown") {
      memo.exec(1);
    }

    SECTION("NoCloexec") {
      memo.open(1, 2, "path", false);
      memo.exec(1);
      CHECK(memo.getFileDescriptorPath(1, 2) == "path");
    }

    SECTION("SetCloexec") {
      memo.open(1, 2, "path", false);
      memo.setCloexec(1, 2, true);
      memo.exec(1);
      CHECK(memo.getFileDescriptorPath(1, 2) == "");
    }

    SECTION("UnsetCloexec") {
      memo.open(1, 2, "path", true);
      memo.setCloexec(1, 2, false);
      memo.exec(1);
      CHECK(memo.getFileDescriptorPath(1, 2) == "path");
    }

    SECTION("Cloexec") {
      memo.open(1, 2, "path", true);
      memo.exec(1);
      CHECK(memo.getFileDescriptorPath(1, 2) == "");
    }

    SECTION("CloexecAndNoCloexec") {
      memo.open(1, 2, "path", false);
      memo.open(1, 3, "path_cloexec", true);
      memo.exec(1);
      CHECK(memo.getFileDescriptorPath(1, 2) == "path");
      CHECK(memo.getFileDescriptorPath(1, 3) == "");
    }
  }

  SECTION("TerminatedUnknown") {
    memo.terminated(1);
  }

  SECTION("Terminate") {
    memo.open(1, 2, "path", false);
    memo.terminated(1);
    CHECK(memo.getFileDescriptorPath(1, 2) == "");
  }

  SECTION("TerminateTwice") {
    memo.open(1, 2, "path", false);
    memo.terminated(1);
    memo.terminated(1);
    CHECK(memo.getFileDescriptorPath(1, 2) == "");
  }

  SECTION("Fork") {
    SECTION("Basic") {
      memo.open(1, 2, "path", false);
      memo.open(1, 3, "path_cloexec", true);
      memo.fork(1, 2);
      CHECK(memo.getFileDescriptorPath(2, 2) == "path");
      CHECK(memo.getFileDescriptorPath(2, 3) == "path_cloexec");
    }

    SECTION("CloseInOriginal") {
      memo.open(1, 2, "path", false);
      memo.open(1, 3, "path_cloexec", true);
      memo.fork(1, 2);
      memo.close(1, 2);
      memo.close(1, 3);
      CHECK(memo.getFileDescriptorPath(1, 2) == "");
      CHECK(memo.getFileDescriptorPath(1, 3) == "");
      CHECK(memo.getFileDescriptorPath(2, 2) == "path");
      CHECK(memo.getFileDescriptorPath(2, 3) == "path_cloexec");
    }

    SECTION("CloseInFork") {
      memo.open(1, 2, "path", false);
      memo.open(1, 3, "path_cloexec", true);
      memo.fork(1, 2);
      memo.close(2, 2);
      memo.close(2, 3);
      CHECK(memo.getFileDescriptorPath(1, 2) == "path");
      CHECK(memo.getFileDescriptorPath(1, 3) == "path_cloexec");
      CHECK(memo.getFileDescriptorPath(2, 2) == "");
      CHECK(memo.getFileDescriptorPath(2, 3) == "");
    }

    SECTION("ClosePriorToFork") {
      memo.open(1, 2, "path", false);
      memo.open(1, 3, "path_cloexec", true);
      memo.close(1, 2);
      memo.close(1, 3);
      memo.fork(1, 2);
      CHECK(memo.getFileDescriptorPath(1, 2) == "");
      CHECK(memo.getFileDescriptorPath(1, 3) == "");
      CHECK(memo.getFileDescriptorPath(2, 2) == "");
      CHECK(memo.getFileDescriptorPath(2, 3) == "");
    }
  }
}

}  // namespace shk
