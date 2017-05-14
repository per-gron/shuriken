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

#include "cache/cache_lookup_result.h"

namespace shk {

TEST_CASE("CacheLookupResult") {
  SECTION("construction") {
    CacheLookupResult empty(0);
    CacheLookupResult one(1);
  }

  SECTION("pop empty") {
    CacheLookupResult lookup(1);
    CHECK(lookup.pop(0) == nullptr);
  }

  SECTION("insert, pop") {
    CacheLookupResult lookup(1);

    auto entry = CacheLookupResult::Entry();
    entry.ignored_dependencies = { 1337 };
    lookup.insert(0, std::move(entry));

    auto result = lookup.pop(0);
    REQUIRE(result);
    CHECK(result->ignored_dependencies == std::vector<uint32_t>{ 1337 });
  }

  SECTION("overwriting insert") {
    CacheLookupResult lookup(1);

    lookup.insert(0, CacheLookupResult::Entry());

    auto entry = CacheLookupResult::Entry();
    entry.ignored_dependencies = { 1337 };
    lookup.insert(0, std::move(entry));

    auto result = lookup.pop(0);
    REQUIRE(result);
    CHECK(result->ignored_dependencies == std::vector<uint32_t>{ 1337 });

    CHECK(!lookup.pop(0));
  }

  SECTION("pop a second time") {
    CacheLookupResult lookup(1);

    lookup.insert(0, CacheLookupResult::Entry());

    CHECK(lookup.pop(0));
    CHECK(!lookup.pop(0));
  }

  SECTION("independent entries") {
    CacheLookupResult lookup(2);

    auto entry_0 = CacheLookupResult::Entry();
    entry_0.ignored_dependencies = { 1337 };
    lookup.insert(0, std::move(entry_0));

    auto entry_1 = CacheLookupResult::Entry();
    entry_1.ignored_dependencies = { 1338 };
    lookup.insert(1, std::move(entry_1));

    auto result_0 = lookup.pop(0);
    REQUIRE(result_0);
    CHECK(result_0->ignored_dependencies == std::vector<uint32_t>{ 1337 });
    auto result_1 = lookup.pop(1);
    REQUIRE(result_1);
    CHECK(result_1->ignored_dependencies == std::vector<uint32_t>{ 1338 });
  }

  SECTION("copy output_files") {
    CacheLookupResult lookup(1);

    Hash a_hash{};
    a_hash.data[0] = 1;

    auto entry = CacheLookupResult::Entry();
    entry.output_files.emplace_back("hello", a_hash);
    lookup.insert(0, std::move(entry));

    auto result = lookup.pop(0);
    REQUIRE(result);
    CHECK(
        result->output_files ==
        (std::vector<std::pair<std::string, Hash>>{ { "hello", a_hash } }));
  }

  SECTION("copy additional_dependencies") {
    CacheLookupResult lookup(1);

    Hash a_hash{};
    a_hash.data[0] = 1;

    auto entry = CacheLookupResult::Entry();
    entry.additional_dependencies.push_back(a_hash);
    lookup.insert(0, std::move(entry));

    auto result = lookup.pop(0);
    REQUIRE(result);
    CHECK(result->additional_dependencies == std::vector<Hash>{ a_hash });
  }

  SECTION("copy provided strings and hashes") {
    CacheLookupResult lookup(1);

    nt_string_view input_path = "hej";
    Hash input_hash{};

    auto entry = CacheLookupResult::Entry();
    entry.input_files.emplace_back(input_path, &input_hash);
    lookup.insert(0, std::move(entry));

    auto result = lookup.pop(0);
    REQUIRE(result);

    // They should be equal
    REQUIRE(result->input_files.size() == 1);
    CHECK(result->input_files[0].first == input_path);
    CHECK(*result->input_files[0].second == input_hash);

    // But have different identity
    CHECK(result->input_files[0].first.data() != input_path.data());
    CHECK(result->input_files[0].second != &input_hash);
  }

  SECTION("deduplicate provided strings and hashes") {
    CacheLookupResult lookup(2);

    nt_string_view input_path = "hej";
    Hash input_hash{};

    std::string input_path_2 = "hej";
    Hash input_hash_2{};

    auto entry_0 = CacheLookupResult::Entry();
    entry_0.input_files.emplace_back(input_path, &input_hash);
    lookup.insert(0, std::move(entry_0));

    auto entry_1 = CacheLookupResult::Entry();
    entry_1.input_files.emplace_back(input_path_2, &input_hash_2);
    lookup.insert(1, std::move(entry_1));

    auto result_0 = lookup.pop(0);
    REQUIRE(result_0);
    auto result_1 = lookup.pop(1);
    REQUIRE(result_1);

    CHECK(result_0->input_files == result_1->input_files);

    REQUIRE(result_0->input_files.size() == 1);
    REQUIRE(result_1->input_files.size() == 1);

    CHECK(
        result_0->input_files[0].first.data() ==
        result_1->input_files[0].first.data());
    CHECK(
      result_0->input_files[0].second ==
      result_1->input_files[0].second);
  }
}

}  // namespace shk
