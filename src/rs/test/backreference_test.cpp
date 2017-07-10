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

#include <string>

#include <rs/backreference.h>

namespace shk {
namespace {

template <typename T>
class WithGenericConstructor {
 public:
  template <typename U>
  explicit WithGenericConstructor(U &&v)
      : t_(std::forward<U>(v)) {}

 private:
  T t_;
};

class Supertype {
 public:
  virtual ~Supertype() = default;

  virtual int GetValue() = 0;
};

class Subtype : public Supertype {
 public:
  int GetValue() override {
    return 1337;
  }
};

}  // anonymous namespace

TEST_CASE("Backreference") {
  SECTION("Backreferee") {
    SECTION("default constructor") {
      Backreferee<std::string> str;
      CHECK(str == "");
    }

    SECTION("destructor") {
      SECTION("with backreference") {
        Backreference<std::string> ref;
        {
          Backreferee<std::string> str = WithBackreference(
              std::string("hey"), &ref);
        }

        CHECK(!ref);
      }

      SECTION("without backreference") {
        Backreference<std::string> ref;
        {
          Backreferee<std::string> str = WithBackreference(
              std::string("hey"), &ref);
          ref.Reset();
        }

        CHECK(!ref);
      }
    }

    SECTION("base operator=") {
      Backreference<std::string> ref;
      Backreferee<std::string> str = WithBackreference(
          std::string("hey"), &ref);

      str = "new";

      CHECK(*ref == "new");
    }

    SECTION("move constructor") {
      SECTION("parameter has backref") {
        Backreference<std::string> ref;
        Backreferee<std::string> str = WithBackreference(
            std::string("hey"), &ref);

        Backreferee<std::string> moved(std::move(str));

        CHECK(str.empty());
        CHECK(*ref == "hey");
        CHECK(moved == "hey");
      }

      SECTION("parameter has no backref") {
        Backreference<std::string> ref;
        Backreferee<std::string> str = WithBackreference(
            std::string("hey"), &ref);
        ref.Reset();

        Backreferee<std::string> moved(std::move(str));

        CHECK(str.empty());
        CHECK(!ref);
        CHECK(moved == "hey");
      }

      SECTION("inner type with generic constructor") {
        Backreference<WithGenericConstructor<int>> ref_a;
        Backreferee<WithGenericConstructor<int>> str_a = WithBackreference(
            WithGenericConstructor<int>(5), &ref_a);

        Backreference<WithGenericConstructor<int>> ref_b;
        Backreferee<WithGenericConstructor<int>> str_b = WithBackreference(
            WithGenericConstructor<int>(6), &ref_b);

        str_a = std::move(str_b);
      }
    }

    SECTION("move assignment operator") {
      SECTION("lhs with backref, rhs with backref") {
        Backreference<std::string> ref_a;
        Backreferee<std::string> str_a = WithBackreference(
            std::string("str_a"), &ref_a);

        Backreference<std::string> ref_b;
        Backreferee<std::string> str_b = WithBackreference(
            std::string("str_b"), &ref_b);

        Backreference<std::string> ref_c;
        Backreferee<std::string> str_c = WithBackreference(
            std::string("str_c"), &ref_c);

        str_a = std::move(str_b);

        str_c = std::move(str_b);  // str_b should not have a backref to ref_b

        CHECK(str_a == "str_b");
        CHECK(str_b.empty());
        CHECK(!ref_a);
        CHECK(*ref_b == "str_b");
      }

      SECTION("lhs without backref, rhs with backref") {
        Backreference<std::string> ref_a;
        Backreferee<std::string> str_a = WithBackreference(
            std::string("str_a"), &ref_a);
        ref_a.Reset();

        Backreference<std::string> ref_b;
        Backreferee<std::string> str_b = WithBackreference(
            std::string("str_b"), &ref_b);

        str_a = std::move(str_b);

        CHECK(str_a == "str_b");
        CHECK(str_b.empty());
        CHECK(!ref_a);
        CHECK(*ref_b == "str_b");
      }

      SECTION("lhs with backref, rhs without backref") {
        Backreference<std::string> ref_a;
        Backreferee<std::string> str_a = WithBackreference(
            std::string("str_a"), &ref_a);

        Backreference<std::string> ref_b;
        Backreferee<std::string> str_b = WithBackreference(
            std::string("str_b"), &ref_b);
        ref_b.Reset();

        str_a = std::move(str_b);

        CHECK(str_a == "str_b");
        CHECK(str_b.empty());
        CHECK(!ref_a);
        CHECK(!ref_b);
      }
    }
  }

  SECTION("Backreference") {
    SECTION("default constructor") {
      Backreference<std::string> backref;
      CHECK(!backref);
    }

    SECTION("destructor") {
      SECTION("with backreferee") {
        Backreference<std::string> ref_a;
        Backreferee<std::string> str_a = WithBackreference(
            std::string("str_a"), &ref_a);

        Backreference<std::string> ref_b;
        Backreferee<std::string> str_b = WithBackreference(
            std::string("str_b"), &ref_b);

        std::make_unique<Backreference<std::string>>(std::move(ref_a));

        // Now, str_a should have no backreference pointer. If it does, it will
        // point to freed memory, which asan will catch here:
        str_b = std::move(str_a);

        CHECK(!ref_a);
        CHECK(!ref_b);
      }

      SECTION("without backreference") {
        Backreference<std::string> ref;
      }
    }

    SECTION("move constructor") {
      SECTION("empty parameter") {
        Backreference<std::string> a;
        Backreference<std::string> b(std::move(a));
        CHECK(!a);
        CHECK(!b);
      }

      SECTION("nonempty parameter") {
        Backreference<std::string> a;
        Backreferee<std::string> str = WithBackreference(
            std::string("hey"), &a);
        Backreference<std::string> b(std::move(a));

        CHECK(!a);
        CHECK(*b == "hey");

        Backreferee<std::string> moved_str(std::move(str));
        CHECK(*b == "hey");
      }
    }

    SECTION("move assignment operator") {
      SECTION("empty lhs and rhs") {
        Backreference<std::string> a;
        Backreference<std::string> b;

        b = std::move(a);

        CHECK(!a);
        CHECK(!b);
      }

      SECTION("empty lhs, nonempty rhs") {
        Backreference<std::string> a;
        Backreferee<std::string> str = WithBackreference(
            std::string("hey"), &a);
        Backreference<std::string> b;

        b = std::move(a);

        CHECK(!a);
        CHECK(*b == "hey");

        Backreferee<std::string> moved_str(std::move(str));
        CHECK(*b == "hey");
      }

      SECTION("nonempty lhs, nonempty rhs") {
        Backreference<std::string> a;
        Backreferee<std::string> str_a = WithBackreference(
            std::string("str_a"), &a);

        Backreference<std::string> b;
        Backreferee<std::string> str_b = WithBackreference(
            std::string("str_b"), &b);

        Backreference<std::string> c;
        Backreferee<std::string> str_c = WithBackreference(
            std::string("str_c"), &c);

        b = std::move(a);

        CHECK(!a);
        CHECK(*b == "str_a");

        str_b = std::move(str_c);
        CHECK(*b == "str_a");
      }
    }

    SECTION("Reset") {
      SECTION("nonempty") {
        Backreference<std::string> a;
        Backreferee<std::string> str_a = WithBackreference(
            std::string("str_a"), &a);

        Backreference<std::string> b;
        Backreferee<std::string> str_b = WithBackreference(
            std::string("str_b"), &b);

        a.Reset();
        CHECK(!a);

        str_b = std::move(str_a);

        CHECK(!a);
      }

      SECTION("empty") {
        Backreference<std::string> a;
        a.Reset();
      }
    }

    SECTION("operator bool") {
      // Already tested by the other unit tests
    }

    SECTION("operator*") {
      Backreference<std::string> a;
      Backreferee<std::string> str_a = WithBackreference(
          std::string("str_a"), &a);

      *a = "new";  // non-const
      const auto &const_a = a;
      CHECK(*const_a == "new");  // const
    }

    SECTION("operator->") {
      Backreference<std::string> a;
      Backreferee<std::string> str_a = WithBackreference(
          std::string("str_a"), &a);

      a->append("_hey");  // non-const
      const auto &const_a = a;
      CHECK(const_a->find("_hey") == 5);  // const
    }
  }

  SECTION("variadric WithBackreference function") {
    SECTION("no backreferences") {
      std::string value = "hello there!";
      auto value_backreferee = WithBackreference(value);
      CHECK(value == value_backreferee);

      static_assert(
          std::is_same<std::string, decltype(value_backreferee)>::value,
          "WithBackreference with no backreference should not wrap");
    }

    SECTION("multiple backreferences") {
      Backreference<std::string> ref_a;
      Backreference<std::string> ref_b;
      auto str = WithBackreference(std::string("str"), &ref_a, &ref_b);

      CHECK(str == "str");
      CHECK(*ref_a == "str");
      CHECK(*ref_b == "str");

      auto str_2 = std::move(str);

      CHECK(str_2 == "str");
      CHECK(*ref_a == "str");
      CHECK(*ref_b == "str");
    }
  }

  SECTION("Backreference to supertype of Backreferee") {
    Backreference<Supertype> a_ref;
    Backreferee<Subtype> a = WithBackreference(
        Subtype(), &a_ref);

    CHECK(a.GetValue() == 1337);
    CHECK(a_ref->GetValue() == 1337);
  }

  SECTION("moving Backreferee and Backreference together") {
    SECTION("reference first") {
      struct Together {
        Together(const std::string &str)
            : str(WithBackreference(str, &ref)) {}

        Backreference<std::string> ref;
        Backreferee<std::string> str;
      };

      Together a("a");
      Together b("b");

      CHECK(*a.ref == "a");
      CHECK(a.str == "a");
      CHECK(*b.ref == "b");
      CHECK(b.str == "b");

      a = std::move(b);
      CHECK(!b.ref);
      CHECK(b.str == "");
      CHECK(*a.ref == "b");
      CHECK(a.str == "b");
    }

    SECTION("referee first") {
      static Backreference<std::string> tmp;

      struct Together {
        Together(const std::string &s)
            : str(WithBackreference(std::string("tmp"), &tmp)) {
          str = WithBackreference(s, &ref);
        }

        Backreferee<std::string> str;
        Backreference<std::string> ref;
      };

      Together a("a");
      Together b("b");

      CHECK(*a.ref == "a");
      CHECK(a.str == "a");
      CHECK(*b.ref == "b");
      CHECK(b.str == "b");

      a = std::move(b);
      CHECK(!b.ref);
      CHECK(b.str == "");
      CHECK(*a.ref == "b");
      CHECK(a.str == "b");
    }
  }
}

}  // namespace shk