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
