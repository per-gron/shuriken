#include <catch.hpp>

#include "log/invocations.h"

namespace shk {

TEST_CASE("Invocations") {
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
      a.input_files.push_back(0);

      CHECK(!(b == a));
      CHECK(!(a == b));
      CHECK(b != a);
      CHECK(a != b);      
    }

    SECTION("OutputFiles") {
      a.output_files.push_back(0);

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
      e.output_files.push_back(0);
      i.entries[Hash()] = e;

      CHECK(i.countUsedFingerprints() == 1);
    }

    SECTION("OneUsedAsInput") {
      Invocations i;
      i.fingerprints.emplace_back();

      Invocations::Entry e;
      e.input_files.push_back(0);
      i.entries[Hash()] = e;

      CHECK(i.countUsedFingerprints() == 1);
    }

    SECTION("OneUsedAsInputAndOutput") {
      Invocations i;
      i.fingerprints.emplace_back();

      Invocations::Entry e;
      e.input_files.push_back(0);
      e.output_files.push_back(0);
      i.entries[Hash()] = e;

      CHECK(i.countUsedFingerprints() == 1);
    }

    SECTION("OneUsedAndOneUnused") {
      Invocations i;
      i.fingerprints.emplace_back();
      i.fingerprints.emplace_back();

      Invocations::Entry e;
      e.input_files.push_back(1);
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
    input_0.input_files.push_back(0);

    Invocations::Entry output_0;
    output_0.input_files.push_back(0);

    Invocations::Entry input_1;
    input_1.input_files.push_back(1);

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
      b_entry.output_files.push_back(0);
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
      b_entry.input_files.push_back(0);
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
      b_entry.output_files.push_back(0);
      b.entries.emplace(Hash(), b_entry);

      a.fingerprints.emplace_back("path", Fingerprint());
      Invocations::Entry a_entry;
      a_entry.output_files.push_back(0);
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
      b_entry.output_files.push_back(1);
      b.entries.emplace(Hash(), b_entry);

      a.fingerprints.emplace_back("path", Fingerprint());
      Invocations::Entry a_entry;
      a_entry.output_files.push_back(0);
      a.entries.emplace(Hash(), a_entry);

      CHECK(b == a);
      CHECK(a == b);
      CHECK(!(b != a));
      CHECK(!(a != b));
    }
  }
}

}  // namespace shk
