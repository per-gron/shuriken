// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "util.h"

namespace shk {

TEST_CASE("Util") {
  SECTION("PathEscaping") {
    SECTION("TortureTest") {
      std::string result;

      getWin32EscapedString("foo bar\\\"'$@d!st!c'\\path'\\", &result);
      CHECK("\"foo bar\\\\\\\"'$@d!st!c'\\path'\\\\\"" == result);
      result.clear();

      getShellEscapedString("foo bar\"/'$@d!st!c'/path'", &result);
      CHECK("'foo bar\"/'\\''$@d!st!c'\\''/path'\\'''" == result);
    }

    SECTION("SensiblePathsAreNotNeedlesslyEscaped") {
      const char* path = "some/sensible/path/without/crazy/characters.c++";
      std::string result;

      getWin32EscapedString(path, &result);
      CHECK(path == result);
      result.clear();

      getShellEscapedString(path, &result);
      CHECK(path == result);
    }

    SECTION("SensibleWin32PathsAreNotNeedlesslyEscaped") {
      const char* path = "some\\sensible\\path\\without\\crazy\\characters.c++";
      std::string result;

      getWin32EscapedString(path, &result);
      CHECK(path == result);
    }
  }

  SECTION("stripAnsiEscapeCodes") {
    SECTION("EscapeAtEnd") {
      std::string stripped = stripAnsiEscapeCodes("foo\33");
      CHECK("foo" == stripped);

      stripped = stripAnsiEscapeCodes("foo\33[");
      CHECK("foo" == stripped);
    }

    SECTION("StripColors") {
      // An actual clang warning.
      std::string input = "\33[1maffixmgr.cxx:286:15: \33[0m\33[0;1;35mwarning: "
                     "\33[0m\33[1musing the result... [-Wparentheses]\33[0m";
      std::string stripped = stripAnsiEscapeCodes(input);
      CHECK("affixmgr.cxx:286:15: warning: using the result... [-Wparentheses]" == stripped);
    }
  }
}

}  // namespace shk
