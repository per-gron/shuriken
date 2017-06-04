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

#include <rs/element_count.h>

namespace shk {

TEST_CASE("ElementCount") {
  static constexpr auto kMax = std::numeric_limits<ElementCount::Value>::max();
  static constexpr auto kMin = std::numeric_limits<ElementCount::Value>::min();

  SECTION("construct") {
    SECTION("default") {
      ElementCount count;
      CHECK(count.Get() == 0);
    }

    SECTION("from integer") {
      ElementCount count(1);
      CHECK(count.Get() == 1);
    }

    SECTION("copy") {
      ElementCount count;
      ElementCount copy(count);
    }

    SECTION("move") {
      ElementCount count;
      ElementCount moved(std::move(count));
    }
  }

  SECTION("assignment") {
    SECTION("copy assign") {
      ElementCount a;
      ElementCount b;
      a = b;
    }

    SECTION("move assign") {
      ElementCount a;
      ElementCount b;
      a = std::move(b);
    }

    SECTION("assign value") {
      ElementCount a;
      CHECK((a = 5).Get() == 5);
    }
  }

  SECTION("infinity") {
    CHECK(ElementCount::Infinite().Get() == kMax);
    CHECK(ElementCount::Infinite().IsInfinite());
    CHECK(ElementCount(kMax).IsInfinite());
    CHECK(!ElementCount(0).IsInfinite());
    CHECK(!ElementCount(kMax - 1).IsInfinite());
    CHECK(!ElementCount(kMin).IsInfinite());
  }

  SECTION("prefix increment") {
    SECTION("from zero") {
      ElementCount zero;
      CHECK((++zero).Get() == 1);
      CHECK(zero.Get() == 1);
    }

    SECTION("from one") {
      ElementCount one(1);
      CHECK((++one).Get() == 2);
      CHECK(one.Get() == 2);
    }

    SECTION("from infinity") {
      ElementCount one(kMax);
      CHECK((++one).Get() == kMax);
      CHECK(one.Get() == kMax);
    }

    SECTION("from min value") {
      ElementCount min(kMin);
      CHECK((++min).Get() == kMin + 1);
      CHECK(min.Get() == kMin + 1);
    }
  }

  SECTION("postfix increment") {
    SECTION("from zero") {
      ElementCount zero;
      CHECK((zero++).Get() == 0);
      CHECK(zero.Get() == 1);
    }

    SECTION("from one") {
      ElementCount one(1);
      CHECK((one++).Get() == 1);
      CHECK(one.Get() == 2);
    }

    SECTION("from infinity") {
      ElementCount one(kMax);
      CHECK((one++).Get() == kMax);
      CHECK(one.Get() == kMax);
    }

    SECTION("from min value") {
      ElementCount min(kMin);
      CHECK((min++).Get() == kMin);
      CHECK(min.Get() == kMin + 1);
    }
  }

  SECTION("prefix decrement") {
    SECTION("from zero") {
      ElementCount zero;
      CHECK((--zero).Get() == -1);
      CHECK(zero.Get() == -1);
    }

    SECTION("from one") {
      ElementCount one(1);
      CHECK((--one).Get() == 0);
      CHECK(one.Get() == 0);
    }

    SECTION("from infinity") {
      ElementCount one(kMax);
      CHECK((--one).Get() == kMax);
      CHECK(one.Get() == kMax);
    }

    SECTION("from min value") {
      ElementCount min(kMin);
      CHECK_THROWS_AS(--min, std::range_error);
    }
  }

  SECTION("addition assignment with value") {
    SECTION("from zero") {
      ElementCount zero;
      CHECK((zero += 2).Get() == 2);
      CHECK(zero.Get() == 2);
    }

    SECTION("negative from zero") {
      ElementCount zero;
      CHECK((zero += -2).Get() == -2);
      CHECK(zero.Get() == -2);
    }

    SECTION("from one") {
      ElementCount one(1);
      CHECK((one += 2).Get() == 3);
      CHECK(one.Get() == 3);
    }

    SECTION("negative from one") {
      ElementCount one(1);
      CHECK((one += -2).Get() == -1);
      CHECK(one.Get() == -1);
    }

    SECTION("from infinity") {
      ElementCount one(kMax);
      CHECK((one += 2).Get() == kMax);
      CHECK(one.Get() == kMax);
    }

    SECTION("negative from infinity") {
      ElementCount one(kMax);
      CHECK((one += -2).Get() == kMax);
      CHECK(one.Get() == kMax);
    }

    SECTION("from min value") {
      ElementCount min(kMin);
      CHECK((min += 2).Get() == kMin + 2);
      CHECK(min.Get() == kMin + 2);
    }

    SECTION("negative from min value") {
      ElementCount min(kMin);
      CHECK_THROWS_AS(min += -1, std::range_error);
    }
  }

  SECTION("addition assignment with ElementCount") {
    SECTION("from zero") {
      ElementCount zero;
      CHECK((zero += ElementCount(2)).Get() == 2);
      CHECK(zero.Get() == 2);
    }

    // This shares logic with addition assignment with value so no need to test
    // all corner cases here.
  }

  SECTION("subtraction assignment with value") {
    SECTION("from zero") {
      ElementCount zero;
      CHECK((zero -= 2).Get() == -2);
      CHECK(zero.Get() == -2);
    }

    SECTION("negative from zero") {
      ElementCount zero;
      CHECK((zero -= -2).Get() == 2);
      CHECK(zero.Get() == 2);
    }

    SECTION("from one") {
      ElementCount one(1);
      CHECK((one -= 2).Get() == -1);
      CHECK(one.Get() == -1);
    }

    SECTION("negative from one") {
      ElementCount one(1);
      CHECK((one -= -2).Get() == 3);
      CHECK(one.Get() == 3);
    }

    SECTION("from infinity") {
      ElementCount one(kMax);
      CHECK((one -= 2).Get() == kMax);
      CHECK(one.Get() == kMax);
    }

    SECTION("negative from infinity") {
      ElementCount one(kMax);
      CHECK((one -= -2).Get() == kMax);
      CHECK(one.Get() == kMax);
    }

    SECTION("from min value") {
      ElementCount min(kMin);
      CHECK_THROWS_AS(min -= 1, std::range_error);
    }

    SECTION("negative from min value") {
      ElementCount min(kMin);
      CHECK((min -= -2).Get() == kMin + 2);
      CHECK(min.Get() == kMin + 2);
    }
  }

  SECTION("subtraction assignment with ElementCount") {
    SECTION("from zero") {
      ElementCount zero;
      CHECK((zero -= ElementCount(2)).Get() == -2);
      CHECK(zero.Get() == -2);
    }

    // This shares logic with subtraction assignment with value so no need to
    // test all corner cases here.
  }

  SECTION("addition") {
    // This shares logic with addition assignment with value so no need to
    // test all corner cases here.

    CHECK((ElementCount(1) + ElementCount(2)).Get() == 3);
    CHECK((ElementCount(1) + 2).Get() == 3);
    CHECK((1 + ElementCount(2)).Get() == 3);
  }

  SECTION("subtraction") {
    // This shares logic with subtraction assignment with value so no need to
    // test all corner cases here.

    CHECK((ElementCount(3) - ElementCount(1)).Get() == 2);
    CHECK((ElementCount(3) - 1).Get() == 2);
    CHECK((3 - ElementCount(1)).Get() == 2);
  }

  SECTION("equality") {
    SECTION("ElementCount vs ElementCount") {
      CHECK(ElementCount(0) == ElementCount(0));
      CHECK(ElementCount::Infinite() == ElementCount::Infinite());
      CHECK(!(ElementCount(0) == ElementCount(1)));

      CHECK(!(ElementCount(0) != ElementCount(0)));
      CHECK(!(ElementCount::Infinite() != ElementCount::Infinite()));
      CHECK(ElementCount(0) != ElementCount(1));
    }

    SECTION("ElementCount vs Value") {
      CHECK(ElementCount(0) == 0);
      CHECK(ElementCount::Infinite() == kMax);
      CHECK(!(ElementCount(0) == 1));

      CHECK(!(ElementCount(0) != 0));
      CHECK(
          !(ElementCount::Infinite() != kMax));
      CHECK(ElementCount(0) != 1);
    }

    SECTION("Value vs ElementCount") {
      CHECK(0 == ElementCount(0));
      CHECK(kMax == ElementCount::Infinite());
      CHECK(!(0 == ElementCount(1)));

      CHECK(!(0 != ElementCount(0)));
      CHECK(
          !(kMax != ElementCount::Infinite()));
      CHECK(0 != ElementCount(1));
    }
  }

  SECTION("compare") {
    SECTION("ElementCount vs ElementCount") {
      SECTION("less than") {
        CHECK(ElementCount(0) < ElementCount(1));
        CHECK(!(ElementCount(0) < ElementCount(0)));
        CHECK(!(ElementCount(1) < ElementCount(0)));
        CHECK(ElementCount(0) < ElementCount::Infinite());
        CHECK(!(ElementCount::Infinite() < ElementCount::Infinite()));
      }

      SECTION("less than or equal") {
        CHECK(ElementCount(0) <= ElementCount(1));
        CHECK(ElementCount(0) <= ElementCount(0));
        CHECK(!(ElementCount(1) <= ElementCount(0)));
        CHECK(ElementCount(0) <= ElementCount::Infinite());
        CHECK(ElementCount::Infinite() <= ElementCount::Infinite());
      }

      SECTION("greater than") {
        CHECK(ElementCount(1) > ElementCount(0));
        CHECK(!(ElementCount(0) > ElementCount(0)));
        CHECK(!(ElementCount(0) > ElementCount(1)));
        CHECK(ElementCount::Infinite() > ElementCount(0));
        CHECK(!(ElementCount::Infinite() > ElementCount::Infinite()));
      }

      SECTION("greater than or equal") {
        CHECK(ElementCount(1) >= ElementCount(0));
        CHECK(ElementCount(0) >= ElementCount(0));
        CHECK(!(ElementCount(0) >= ElementCount(1)));
        CHECK(ElementCount::Infinite() >= ElementCount(0));
        CHECK(ElementCount::Infinite() >= ElementCount::Infinite());
      }
    }

    SECTION("ElementCount vs Value") {
      SECTION("less than") {
        CHECK(ElementCount(0) < 1);
        CHECK(!(ElementCount(0) < 0));
        CHECK(!(ElementCount(1) < 0));
        CHECK(ElementCount(0) < kMax);
        CHECK(!(ElementCount::Infinite() < kMax));
      }

      SECTION("less than or equal") {
        CHECK(ElementCount(0) <= 1);
        CHECK(ElementCount(0) <= 0);
        CHECK(!(ElementCount(1) <= 0));
        CHECK(ElementCount(0) <= kMax);
        CHECK(ElementCount::Infinite() <= kMax);
      }

      SECTION("greater than") {
        CHECK(ElementCount(1) > 0);
        CHECK(!(ElementCount(0) > 0));
        CHECK(!(ElementCount(0) > 1));
        CHECK(ElementCount::Infinite() > 0);
        CHECK(!(ElementCount::Infinite() > kMax));
      }

      SECTION("greater than or equal") {
        CHECK(ElementCount(1) >= 0);
        CHECK(ElementCount(0) >= 0);
        CHECK(!(ElementCount(0) >= 1));
        CHECK(ElementCount::Infinite() >= 0);
        CHECK(ElementCount::Infinite() >= kMax);
      }
    }

    SECTION("Value vs ElementCount") {
      SECTION("less than") {
        CHECK(0 < ElementCount(1));
        CHECK(!(0 < ElementCount(0)));
        CHECK(!(1 < ElementCount(0)));
        CHECK(0 < ElementCount::Infinite());
        CHECK(!(kMax < ElementCount::Infinite()));
      }

      SECTION("less than or equal") {
        CHECK(0 <= ElementCount(1));
        CHECK(0 <= ElementCount(0));
        CHECK(!(1 <= ElementCount(0)));
        CHECK(0 <= ElementCount::Infinite());
        CHECK(kMax <= ElementCount::Infinite());
      }

      SECTION("greater than") {
        CHECK(1 > ElementCount(0));
        CHECK(!(0 > ElementCount(0)));
        CHECK(!(0 > ElementCount(1)));
        CHECK(kMax > ElementCount(0));
        CHECK(!(kMax > ElementCount::Infinite()));
      }

      SECTION("greater than or equal") {
        CHECK(1 >= ElementCount(0));
        CHECK(0 >= ElementCount(0));
        CHECK(!(0 >= ElementCount(1)));
        CHECK(kMax >= ElementCount(0));
        CHECK(kMax >= ElementCount::Infinite());
      }
    }
  }
}

}  // namespace shk
