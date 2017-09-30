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

#pragma once

#include <functional>
#include <string>

#include <sys/types.h>

#include <util/hash.h>
#include <util/string_view.h>

#include "fs/file_id.h"
#include "fs/file_system.h"

namespace shk {
namespace detail {

void computeFingerprintHash(
    FileSystem &file_system,
    size_t file_size,
    mode_t mode,
    nt_string_view path,
    Hash *hash) throw(IoError);

}

/**
 * A Fingerprint is information about a file that Shuriken stores in the
 * invocation log. It contains information that can be used to detect if the
 * file has been modified (or started or ceased existing) since the Fingerprint
 * was last taken. This is the basis of what Shuriken uses to find out if a
 * build step has become dirty and needs to be re-invoked.
 *
 * Unlike Ninja, which only uses file timestamps, Shuriken uses (a hash of) the
 * contents of the file to do dirtiness checking. The reason Shuriken does not
 * rely only on timestamps is the same as most of the other changes compared to
 * Ninja: It is possible for builds to do the wrong thing when using only
 * timestamps. This can happen if a file is modified within the same second as
 * the build of it finished. Then Ninja will not see that the file has changed.
 *
 * The algorithm that Shuriken uses is inspired by the one used by git:
 * https://www.kernel.org/pub/software/scm/git/docs/technical/racy-git.txt
 *
 * When performing a no-op build, this algorithm allows Shuriken to usually not
 * have to do more than stat-ing inputs and outputs before it can decide that
 * nothing has to be done.
 *
 * Fingerprint objects are stored as-is to disk in the invocation log, so they
 * must be POD objects with no pointers. Changing the contents of Fingerprint
 * results in a breaking change to the invocation log format.
 */
struct Fingerprint {
  /**
   * Fingerprint::Stat is a subset of the full Stat information. It contains
   * only things that Fingerprints are concerned with. For example, it does not
   * contain st_dev, because it's not stable over time on network file systems.
   */
  struct Stat {
    Stat();

    static void fromStat(const ::shk::Stat &stat, Stat *out);

    size_t size = 0;
    ino_t ino = 0;
    /**
     * Contains only a subset of the st_mode data, but it contains enough to be
     * able to probe with S_ISDIR.
     */
    mode_t mode = 0;
    time_t mtime = 0;

    /**
     * Returns true if the file was successfully stat-ed. False for example if
     * the file does not exist.
     */
    bool couldAccess() const;

    bool isDir() const;

    bool operator==(const Stat &other) const;
    bool operator!=(const Stat &other) const;
    bool operator<(const Stat &other) const;
  };

  Stat stat;
  /**
   * True if the fingeprint was taken at the same time as (or before) the file's
   * mtime.
   */
  bool racily_clean = false;
  /**
   * Has a hash of the file contents along with some stat information. The hash
   * contains enough information so that if two fingerprints' hashes are
   * identical, then the fingerprints match.
   *
   * The stat info embedded in the hash includes file size and permissions. It
   * does not include inode number or mtime or other information that is in the
   * Fingerprint only to quickly be able to validate that a file has not
   * changed.
   */
  Hash hash;

  bool operator==(const Fingerprint &other) const;
  bool operator!=(const Fingerprint &other) const;
  bool operator<(const Fingerprint &other) const;
};

struct MatchesResult {
  bool clean = false;

  /**
   * Set to true if fingerprintMatch had to do an (expensive) file content
   * hashing operation in order to know if the fingerprint is clean. In these
   * situations it is beneficial to recompute the fingerprint for the file.
   * There is then a good chance that hashing will no longer be needed later.
   */
  bool should_update = false;

  /**
   * FileId of the path that the fingerprint refers to.
   */
  FileId file_id;

  bool operator==(const MatchesResult &other) const;
  bool operator!=(const MatchesResult &other) const;
};

/**
 * Take the fingerprint of a file.
 */
std::pair<Fingerprint, FileId> takeFingerprint(
    FileSystem &file_system,
    time_t timestamp,
    nt_string_view path) throw(IoError);

/**
 * Like takeFingerprint, but uses old_fingerprint if possible. If
 * old_fingerprint is clean and not should_update, this function returns an
 * exact copy of it.
 *
 * This is useful when the user of the function already has a Fingerprint of a
 * file but needs to get a Fingerprint that is up to date. If old_fingerprint is
 * clean, then this function is significantly faster than takeFingerprint,
 * because it only has to do a stat rather than a full hash of the file.
 */
std::pair<Fingerprint, FileId> retakeFingerprint(
    FileSystem &file_system,
    time_t timestamp,
    nt_string_view path,
    const Fingerprint &old_fingerprint);

/**
 * Check if a file still matches a given fingerprint.
 */
MatchesResult fingerprintMatches(
    FileSystem &file_system,
    nt_string_view path,
    const Fingerprint &fingerprint) throw(IoError);

/**
 * Check if a given fingerprint is still clean given a Fingerprint::Stat and a
 * Hash of a file.
 */
bool fingerprintMatches(
    const Fingerprint &original_fingerprint,
    const Fingerprint::Stat &new_stat,
    const Hash &new_hash);

/**
 * Check if a given fingerprint is still clean given a Stat and a Hash of a
 * file.
 */
bool fingerprintMatches(
    const Fingerprint &original_fingerprint,
    const Stat &new_stat,
    const Hash &new_hash);

}  // namespace shk

namespace std {

template<>
struct hash<shk::Fingerprint> {
  using argument_type = shk::Fingerprint;
  using result_type = std::size_t;

  result_type operator()(const argument_type &f) const {
    return hash<shk::Hash>()(f.hash) ^ f.racily_clean;
  }
};

}  // namespace std
