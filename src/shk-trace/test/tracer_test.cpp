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

#include "tracer.h"

#include "mock_tracer_delegate.h"

namespace shk {

TEST_CASE("Tracer") {
  // The Kdebug "API" isn't exactly intuitive. Because of this, I think the most
  // likely source of bugs is integration bugs with Kdebug, more than bugs that
  // are not because of a misunderstanding of how Kdebug works. Because of this
  // I have put most of the effort of testing this class into writing
  // integration tests. There are integration test for almost all syscalls
  // covering more or less all branches in Tracer.
  //
  // This unit test suite is here as a complement, for testing things that are
  // difficult or impossible to trigger in an integration test, for example
  // HFS_update behavior.

  int dead_tracers = 0;
  MockTracerDelegate delegate(dead_tracers);
  Tracer tracer(delegate);

  const auto parse = [&](const std::vector<kd_buf> &buffers) {
    tracer.parseBuffer(buffers.data(), buffers.data() + buffers.size());
  };

  static constexpr uint32_t parent_thread_id = 321;
  static constexpr uint32_t child_thread_id = 123;
  static constexpr uint32_t pid = 1337;

  SECTION("parse no buffers") {
    parse({});
  }

  SECTION("parse ignored event") {
    parse({ { .debugid = 0 } });
  }

  SECTION("thread handling") {
    SECTION("new thread") {
      parse({ {
          .debugid = TRACE_DATA_NEWTHREAD,
          .arg1 = child_thread_id,
          .arg2 = pid,
          .arg5 = parent_thread_id } });

      auto thread_event = delegate.popNewThreadEvent();
      CHECK(thread_event.pid == pid);
      CHECK(thread_event.parent_thread_id == parent_thread_id);
      CHECK(thread_event.child_thread_id == child_thread_id);
    }

    SECTION("ignore new thread events without child thread") {
      parse({ {
          .debugid = TRACE_DATA_NEWTHREAD,
          .arg2 = pid,
          .arg5 = parent_thread_id, } });
    }

    SECTION("terminate thread") {
      parse({ {
          .debugid = BSC_thread_terminate,
          .arg5 = parent_thread_id } });

      CHECK(delegate.popTerminateThreadEvent() == parent_thread_id);
    }

    SECTION("fail when creating thread with outstanding events") {
      CHECK_THROWS(parse({
          {
            .debugid = DBG_FUNC_START | BSC_access,
            .arg5 = child_thread_id,
          },
          {
            .debugid = TRACE_DATA_NEWTHREAD,
            .arg1 = child_thread_id,
            .arg2 = pid,
            .arg5 = parent_thread_id,
          } }));
      delegate.popNewThreadEvent();
    }

    SECTION("fail when terminating thread with outstanding events") {
      CHECK_THROWS(parse({
          {
            .debugid = TRACE_DATA_NEWTHREAD,
            .arg1 = child_thread_id,
            .arg2 = pid,
            .arg5 = parent_thread_id,
          },
          {
            .debugid = DBG_FUNC_START | BSC_access,
            .arg5 = child_thread_id,
          },
          {
            .debugid = BSC_thread_terminate,
            .arg5 = child_thread_id,
          } }));
      delegate.popNewThreadEvent();
    }
  }

  SECTION("basic syscall") {
    static constexpr uint32_t from_fd = 1000;
    static constexpr uint32_t to_fd = 1001;

    parse({
        {
          .debugid = TRACE_DATA_NEWTHREAD,
          .arg1 = child_thread_id,
          .arg2 = pid,
          .arg5 = parent_thread_id,
        },
        {
          .debugid = DBG_FUNC_START | BSC_dup,
          .arg1 = from_fd,
          .arg5 = child_thread_id,
        },
        {
          .debugid = DBG_FUNC_END | BSC_dup,
          .arg1 = 0,  // Indicates success
          .arg2 = to_fd,
          .arg5 = child_thread_id,
        } });

    delegate.popNewThreadEvent();
    auto dup = delegate.popDupEvent();
    CHECK(dup.thread_id == child_thread_id);
    CHECK(dup.from_fd == from_fd);
    CHECK(dup.to_fd == to_fd);
    CHECK(dup.cloexec == false);
  }

  SECTION("VFS lookup") {
    static constexpr uint32_t vnode_id = 555;

    const auto str_to_ptr = [](const std::string &str) {
      REQUIRE(str.size() <= 8);
      uintptr_t ans = 0;
      for (int i = 0; i < str.size(); i++) {
        ans |= static_cast<uintptr_t>(str[i]) << (i * 8);
      }
      return ans;
    };

    SECTION("root is default path") {
      parse({
          {
            .debugid = TRACE_DATA_NEWTHREAD,
            .arg1 = child_thread_id,
            .arg2 = pid,
            .arg5 = parent_thread_id,
          },
          {
            .debugid = DBG_FUNC_START | BSC_access,
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_END | BSC_access,
            .arg1 = 0,  // Indicates success
            .arg5 = child_thread_id,
          } });

      delegate.popNewThreadEvent();
      auto evt = delegate.popFileEvent();
      CHECK(evt.thread_id == child_thread_id);
      CHECK(evt.type == EventType::READ);
      CHECK(evt.at_fd == AT_FDCWD);
      CHECK(evt.path == "/");
    }

    SECTION("basic") {
      parse({
          {
            .debugid = TRACE_DATA_NEWTHREAD,
            .arg1 = child_thread_id,
            .arg2 = pid,
            .arg5 = parent_thread_id,
          },
          {
            .debugid = DBG_FUNC_START | BSC_access,
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_START | VFS_LOOKUP,
            .arg1 = vnode_id,
            .arg2 = str_to_ptr("/a_littl"),
            .arg3 = str_to_ptr("e_path./"),
            .arg4 = str_to_ptr("yoyoyoyo"),
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_END | VFS_LOOKUP,
            .arg1 = str_to_ptr(".txt"),
            .arg2 = 0,
            .arg3 = 0,
            .arg4 = 0,
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_END | BSC_access,
            .arg1 = 0,  // Indicates success
            .arg5 = child_thread_id,
          } });

      delegate.popNewThreadEvent();
      auto evt = delegate.popFileEvent();
      CHECK(evt.thread_id == child_thread_id);
      CHECK(evt.type == EventType::READ);
      CHECK(evt.at_fd == AT_FDCWD);
      CHECK(evt.path == "/a_little_path./yoyoyoyo.txt");
    }

    SECTION("with interspersed HFS_update") {
      parse({
          {
            .debugid = TRACE_DATA_NEWTHREAD,
            .arg1 = child_thread_id,
            .arg2 = pid,
            .arg5 = parent_thread_id,
          },
          {
            .debugid = DBG_FUNC_START | BSC_access,
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_START | HFS_update,
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_START | VFS_LOOKUP,
            .arg1 = vnode_id,
            .arg2 = str_to_ptr("/hfs_upd"),
            .arg3 = str_to_ptr("ate_path"),
            .arg4 = str_to_ptr("_that_sh"),
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_END | VFS_LOOKUP,
            .arg1 = str_to_ptr("ould_be_"),
            .arg2 = str_to_ptr("ignored."),
            .arg3 = str_to_ptr("txt"),
            .arg4 = 0,
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_END | HFS_update,
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_START | VFS_LOOKUP,
            .arg1 = vnode_id,
            .arg2 = str_to_ptr("/a_littl"),
            .arg3 = str_to_ptr("e_path./"),
            .arg4 = str_to_ptr("yoyoyoyo"),
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_END | VFS_LOOKUP,
            .arg1 = str_to_ptr(".txt"),
            .arg2 = 0,
            .arg3 = 0,
            .arg4 = 0,
            .arg5 = child_thread_id,
          },
          {
            .debugid = DBG_FUNC_END | BSC_access,
            .arg1 = 0,  // Indicates success
            .arg5 = child_thread_id,
          } });

      delegate.popNewThreadEvent();
      auto evt = delegate.popFileEvent();
      CHECK(evt.thread_id == child_thread_id);
      CHECK(evt.type == EventType::READ);
      CHECK(evt.at_fd == AT_FDCWD);
      CHECK(evt.path == "/a_little_path./yoyoyoyo.txt");
    }
  }
}

}  // namespace shk
