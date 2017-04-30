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

#include "log/invocations.h"

namespace shk {

TEST_CASE("Invocations") {
  const uint32_t a_zero_buf = 0;
  const FingerprintIndicesView a_zero_view(&a_zero_buf, &a_zero_buf + 1);
  const uint32_t a_one_buf = 1;
  const FingerprintIndicesView a_one_view(&a_one_buf, &a_one_buf + 1);

  SECTION("Entry") {
    Invocations::Entry a;
    Invocations::Entry b;

    SECTION("Same") {
      CHECK(a == a);
      CHECK(!(a != a));
    }

    SECTION("Copy") {
      CHECK(b == a);
      CHECK(a == b);
      CHECK(!(b != a));
      CHECK(!(a != b));
    }

    SECTION("InputFiles") {
      a.input_files = a_zero_view;

      CHECK(!(b == a));
      CHECK(!(a == b));
      CHECK(b != a);
      CHECK(a != b);      
    }

    SECTION("OutputFiles") {
      a.output_files = a_zero_view;

      CHECK(!(b == a));
      CHECK(!(a == b));
      CHECK(b != a);
      CHECK(a != b);      
    }
  }

  SECTION("CountUsedFingerprints") {
    SECTION("Empty") {
      CHECK(Invocations().countUsedFingerprints() == 0);
    }

    SECTION("OneUnused") {
      Invocations i;
      i.fingerprints.emplace_back();

      CHECK(i.countUsedFingerprints() == 0);
    }

    SECTION("OneUsedAsOutput") {
      Invocations i;
      i.fingerprints.emplace_back();

      Invocations::Entry e;
      e.output_files = a_zero_view;
      i.entries[Hash()] = e;

      CHECK(i.countUsedFingerprints() == 1);
    }

    SECTION("OneUsedAsInput") {
      Invocations i;
      i.fingerprints.emplace_back();

      Invocations::Entry e;
      e.input_files = a_zero_view;
      i.entries[Hash()] = e;

      CHECK(i.countUsedFingerprints() == 1);
    }

    SECTION("OneUsedAsInputAndOutput") {
      Invocations i;
      i.fingerprints.emplace_back();

      Invocations::Entry e;
      e.input_files = a_zero_view;
      e.output_files = a_zero_view;
      i.entries[Hash()] = e;

      CHECK(i.countUsedFingerprints() == 1);
    }

    SECTION("OneUsedAndOneUnused") {
      Invocations i;
      i.fingerprints.emplace_back();
      i.fingerprints.emplace_back();

      Invocations::Entry e;
      e.input_files = a_one_view;
      i.entries[Hash()] = e;

      CHECK(i.countUsedFingerprints() == 1);
    }
  }

  SECTION("FingerprintsFor") {
    Invocations i;
    i.fingerprints.emplace_back();
    i.fingerprints.emplace_back();
    i.fingerprints.emplace_back();
    i.fingerprints.emplace_back();

    Invocations::Entry empty;

    Invocations::Entry input_0;
    input_0.input_files = a_zero_view;

    Invocations::Entry output_0;
    output_0.input_files = a_zero_view;

    Invocations::Entry input_1;
    input_1.input_files = a_one_view;

    SECTION("EmptyInvocations") {
      CHECK(Invocations().fingerprintsFor({}) == std::vector<uint32_t>{});
    }

    SECTION("EmptyEntriesList") {
      CHECK(i.fingerprintsFor({}) == std::vector<uint32_t>{});
    }

    SECTION("EmptyEntry") {
      CHECK(i.fingerprintsFor({ &empty }) == std::vector<uint32_t>{});
    }

    SECTION("OneInput") {
      CHECK(i.fingerprintsFor({ &input_0 }) == std::vector<uint32_t>{ 0 });
    }

    SECTION("OneOutput") {
      CHECK(i.fingerprintsFor({ &output_0 }) == std::vector<uint32_t>{ 0 });
    }

    SECTION("SeparateInputs") {
      CHECK(
          i.fingerprintsFor({ &input_0, &input_1 }) ==
          std::vector<uint32_t>({ 0, 1 }));
    }

    SECTION("SharedFingerprints") {
      CHECK(
          i.fingerprintsFor({ &input_0, &output_0 }) ==
          std::vector<uint32_t>({ 0 }));
    }

    SECTION("DuplicateInput") {
      CHECK(
          i.fingerprintsFor({ &input_0, &input_0 }) ==
          std::vector<uint32_t>({ 0 }));
    }
  }

  SECTION("Equals") {
    Invocations a;
    Invocations b;

    SECTION("Same") {
      CHECK(a == a);
      CHECK(!(a != a));
    }

    SECTION("Copy") {
      CHECK(b == a);
      CHECK(a == b);
      CHECK(!(b != a));
      CHECK(!(a != b));
    }

    SECTION("CreatedDirectories") {
      Invocations b;
      b.created_directories.emplace(FileId(), "hej");

      CHECK(!(b == a));
      CHECK(!(a == b));
      CHECK(b != a);
      CHECK(a != b);
    }

    SECTION("Entries") {
      Invocations b;
      b.entries.emplace(Hash(), Invocations::Entry());

      CHECK(!(b == a));
      CHECK(!(a == b));
      CHECK(b != a);
      CHECK(a != b);
    }

    SECTION("Fingerprints") {
      Invocations b;
      b.fingerprints.emplace_back("path", Fingerprint());

      CHECK(b == a);
      CHECK(a == b);
      CHECK(!(b != a));
      CHECK(!(a != b));
    }

    SECTION("EntriesDifferentOutputCounts") {
      Invocations b;
      b.fingerprints.emplace_back("path", Fingerprint());
      Invocations::Entry b_entry;
      b_entry.output_files = a_zero_view;
      b.entries.emplace(Hash(), b_entry);

      a.entries.emplace(Hash(), Invocations::Entry());

      CHECK(!(b == a));
      CHECK(!(a == b));
      CHECK(b != a);
      CHECK(a != b);
    }

    SECTION("EntriesDifferentInputCounts") {
      Invocations b;
      b.fingerprints.emplace_back("path", Fingerprint());
      Invocations::Entry b_entry;
      b_entry.input_files = a_zero_view;
      b.entries.emplace(Hash(), b_entry);

      a.entries.emplace(Hash(), Invocations::Entry());

      CHECK(!(b == a));
      CHECK(!(a == b));
      CHECK(b != a);
      CHECK(a != b);
    }

    SECTION("EntriesDifferentPaths") {
      Invocations b;
      b.fingerprints.emplace_back("b_path", Fingerprint());
      Invocations::Entry b_entry;
      b_entry.output_files = a_zero_view;
      b.entries.emplace(Hash(), b_entry);

      a.fingerprints.emplace_back("path", Fingerprint());
      Invocations::Entry a_entry;
      a_entry.output_files = a_zero_view;
      a.entries.emplace(Hash(), a_entry);

      CHECK(!(b == a));
      CHECK(!(a == b));
      CHECK(b != a);
      CHECK(a != b);
    }

    SECTION("SematicallyEqualEntries") {
      Invocations b;
      b.fingerprints.emplace_back("b_path", Fingerprint());
      b.fingerprints.emplace_back("path", Fingerprint());
      Invocations::Entry b_entry;
      b_entry.output_files = a_one_view;
      b.entries.emplace(Hash(), b_entry);

      a.fingerprints.emplace_back("path", Fingerprint());
      Invocations::Entry a_entry;
      a_entry.output_files = a_zero_view;
      a.entries.emplace(Hash(), a_entry);

      CHECK(b == a);
      CHECK(a == b);
      CHECK(!(b != a));
      CHECK(!(a != b));
    }
  }
}

}  // namespace shk
