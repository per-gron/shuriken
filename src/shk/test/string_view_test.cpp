#include <catch.hpp>

#include "string_view.h"

namespace shk {

TEST_CASE("string_view") {
  SECTION("nt_string_view") {
    SECTION("default constructor") {
      nt_string_view v;
      REQUIRE(v.data() != nullptr);
      CHECK(v.data()[0] == 0);
      CHECK(v.size() == 0);
      CHECK(v.null_terminated());
    }

    SECTION("from C string") {
      nt_string_view v("hej");
      REQUIRE(v.data() == "hej");
      CHECK(v.size() == 3);
      CHECK(v.null_terminated());
    }

    SECTION("from C string, cut short") {
      nt_string_view v("hej", 2);
      REQUIRE(v.data() == "hej");
      CHECK(v.size() == 2);
      CHECK(!v.null_terminated());
    }

    SECTION("from C++ string, cut short") {
      std::string str("hej");
      nt_string_view v(str);
      REQUIRE(v.data() == str.data());
      CHECK(v.size() == 3);
      CHECK(v.null_terminated());
    }
  }
}

}  // namespace shk
