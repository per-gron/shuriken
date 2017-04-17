#include <catch.hpp>

#include <vector>

#include "manifest/wrapper_view.h"
#include "string_view.h"

namespace shk {

TEST_CASE("WrapperView") {
  using WV = WrapperView<std::vector<std::string>::const_iterator, string_view>;
  auto empty = std::vector<std::string>{};
  auto one = std::vector<std::string>{ "hi" };
  auto two = std::vector<std::string>{ "one", "two" };

  SECTION("iterator") {
    using Iter = WV::iterator;

    SECTION("Constructor") {
      SECTION("By iterator") {
        Iter(empty.begin());
      }

      SECTION("Copy") {
        auto a = Iter(empty.begin());
        auto b = a;
        CHECK(a == b);
      }

      SECTION("Move") {
        auto a = Iter(empty.begin());
        auto b = std::move(a);
        CHECK(a == b);
      }
    }

    SECTION("Assign") {
      SECTION("Copy") {
        auto a = Iter(empty.begin());
        auto b = Iter(one.begin());
        b = a;
        CHECK(a == b);
      }

      SECTION("Move") {
        auto a = Iter(empty.begin());
        auto b = Iter(one.begin());
        b = std::move(a);
        CHECK(b == Iter(empty.begin()));
      }
    }

    SECTION("swap") {
      auto a = Iter(one.begin());
      auto b = Iter(two.begin());

      std::swap(a, b);

      CHECK(a == Iter(two.begin()));
      CHECK(b == Iter(one.begin()));
    }

    SECTION("operator*") {
      CHECK(*Iter(one.begin()) == "hi");
    }

    SECTION("prefix operator++") {
      auto a = Iter(one.begin());
      CHECK(++a == Iter(one.end()));
    }

    SECTION("postfix operator++") {
      auto a = Iter(one.begin());
      CHECK(a++ == Iter(one.begin()));
      CHECK(a == Iter(one.end()));
    }

    SECTION("prefix operator--") {
      auto a = Iter(one.end());
      CHECK(--a == Iter(one.begin()));
    }

    SECTION("postfix operator--") {
      auto a = Iter(one.end());
      CHECK(a-- == Iter(one.end()));
      CHECK(a == Iter(one.begin()));
    }

    SECTION("operator==") {
      CHECK(Iter(one.begin()) == Iter(one.begin()));
      CHECK(!(Iter(one.begin()) == Iter(one.end())));
    }

    SECTION("operator!=") {
      CHECK(!(Iter(one.begin()) != Iter(one.begin())));
      CHECK(Iter(one.begin()) != Iter(one.end()));
    }

    SECTION("operator+") {
      CHECK(Iter(one.begin()) + 1 == Iter(one.end()));
      CHECK(Iter(one.begin()) + 0 == Iter(one.begin()));
    }

    SECTION("diff_type operator-") {
      CHECK(Iter(one.end()) - 1 == Iter(one.begin()));
      CHECK(Iter(one.end()) - 0 == Iter(one.end()));
    }

    SECTION("iterator operator-") {
      CHECK(Iter(one.end()) - Iter(one.begin()) == 1);
      CHECK(Iter(one.end()) - Iter(one.end()) == 0);
    }
  }

  SECTION("Constructor") {
    SECTION("Default") {
      auto v = WV();
      CHECK(v.empty());
      CHECK(v.begin() == v.end());
    }

    SECTION("Range") {
      WV(empty.begin(), empty.end());
    }

    SECTION("Copy") {
      auto a = WV(empty.begin(), empty.end());
      auto b = a;
      CHECK(a.begin() == b.begin());
      CHECK(a.end() == b.end());
    }

    SECTION("Move") {
      auto a = WV(empty.begin(), empty.end());
      auto begin = a.begin();
      auto end = a.end();
      auto b = std::move(a);
      CHECK(b.begin() == begin);
      CHECK(b.end() == end);
    }
  }

  SECTION("Assign") {
    SECTION("Copy") {
      auto a = WV(empty.begin(), empty.end());
      auto b = WV(one.begin(), one.end());
      a = b;
      CHECK(*(a.begin()) == "hi" );
      CHECK(a.begin() == a.end() - 1);
    }

    SECTION("Move") {
      auto a = WV(empty.begin(), empty.end());
      auto b = WV(one.begin(), one.end());
      a = std::move(b);
      CHECK(*(a.begin()) == "hi" );
      CHECK(a.begin() == a.end() - 1);
    }
  }

  SECTION("at") {
    auto v = WV(one.begin(), one.end());
    CHECK(v.at(0) == "hi");
    CHECK_THROWS_AS(v.at(1), std::out_of_range);
  }

  SECTION("operator[]") {
    auto v = WV(two.begin(), two.end());
    CHECK(v[0] == "one");
    CHECK(v[1] == "two");
  }

  SECTION("front") {
    auto v = WV(two.begin(), two.end());
    CHECK(v.front() == "one");
  }

  SECTION("back") {
    auto v = WV(two.begin(), two.end());
    CHECK(v.back() == "two");
  }

  SECTION("begin") {
    auto v = WV(two.begin(), two.end());
    CHECK(*(v.begin()) == "one");
    CHECK(*(v.begin() + 1) == "two");
  }

  SECTION("cbegin") {
    auto v = WV(two.begin(), two.end());
    CHECK(*(v.cbegin()) == "one");
    CHECK(*(v.cbegin() + 1) == "two");
  }

  SECTION("end") {
    auto v = WV(two.begin(), two.end());
    CHECK(v.begin() + 2 == v.end());
  }

  SECTION("end") {
    auto v = WV(two.begin(), two.end());
    CHECK(v.cbegin() + 2 == v.cend());
  }

  SECTION("empty") {
    CHECK(WV(one.begin(), one.begin()).empty());
    CHECK(!WV(one.begin(), one.end()).empty());
  }

  SECTION("size") {
    CHECK(WV(empty.begin(), empty.end()).size() == 0);
    CHECK(WV(one.begin(), one.end()).size() == 1);
    CHECK(WV(two.begin(), two.end()).size() == 2);
  }

  SECTION("swap") {
    auto a = WV(one.begin(), one.end());
    auto b = WV(two.begin(), two.end());

    CHECK(a[0] == "hi");
    CHECK(b[0] == "one");

    std::swap(a, b);

    CHECK(a[0] == "one");
    CHECK(a.begin() + a.size() == a.end());
    CHECK(b[0] == "hi");
    CHECK(b.begin() + b.size() == b.end());
  }

  SECTION("operator==, operator!=") {
    SECTION("self") {
      auto a = WV(one.begin(), one.end());
      CHECK(a == a);
      CHECK(!(a != a));
    }

    SECTION("same iterators") {
      auto a = WV(one.begin(), one.end());
      auto b = WV(one.begin(), one.end());
      CHECK(a == b);
      CHECK(!(a != b));
    }

    SECTION("same values") {
      auto one_copy = one;

      auto a = WV(one.begin(), one.end());
      auto b = WV(one_copy.begin(), one_copy.end());
      CHECK(a == b);
      CHECK(!(a != b));
    }

    SECTION("different values") {
      auto one_copy = one;
      one_copy.front() += "!!";

      auto a = WV(one.begin(), one.end());
      auto b = WV(one_copy.begin(), one_copy.end());
      CHECK(!(a == b));
      CHECK(a != b);
    }

    SECTION("different sizes") {
      auto one_copy = one;
      one_copy.push_back("lol");

      auto a = WV(one.begin(), one.end());
      auto b = WV(one_copy.begin(), one_copy.end());
      CHECK(!(a == b));
      CHECK(a != b);
    }
  }
}

}  // namespace shk
