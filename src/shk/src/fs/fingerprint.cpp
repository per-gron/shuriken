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

#include "fs/fingerprint.h"

#include <sys/stat.h>

namespace shk {
namespace {

void fingerprintStat(
    FileSystem &file_system,
    nt_string_view path,
    Fingerprint::Stat *out,
    FileId *file_id) throw(IoError) {
  const auto stat = file_system.lstat(path);
  *file_id = FileId(stat);

  Fingerprint::Stat::fromStat(stat, out);
}

/**
 * fingerprintMatches logic that is shared between fingerprintMatches and
 * retakeFingerprint. If the return value MatchesResult::clean is true, then
 * the hash output parameter is set to the hash value of the file that the
 * function had to compute to detect if it was clean or not.
 */
MatchesResult fingerprintMatches(
    FileSystem &file_system,
    nt_string_view path,
    const Fingerprint &fp,
    const Stat &current_stat,
    Hash *hash) throw(IoError) {
  Fingerprint::Stat current_fp_stat;
  Fingerprint::Stat::fromStat(current_stat, &current_fp_stat);

  MatchesResult result;
  result.file_id = FileId(current_stat);
  if (current_fp_stat == fp.stat && (!fp.racily_clean || fp.stat.mode == 0)) {
    // The file's current stat information and the stat information of the
    // fingerprint exactly match. Furthermore, the fingerprint is strictly
    // newer than the files. This means that unless mtime has been tampered
    // with, we know for sure that the file has not been modified since the
    // fingerprint was taken.
    result.clean = true;
  } else {
    // This branch is hit either when we know for sure that the file has been
    // touched since the fingerprint was taken (current_fp_stat != fp.stat) or
    // when the file is "racily clean" (current_fp_stat == fp.stat but the
    // fingerprint was taken less than one second after the file was last
    // modified).
    //
    // If the file is racily clean, it is not possible to tell if the file
    // matches the fingerprint by looking at stat information only, need to fall
    // back on a file content comparison.
    if (current_fp_stat.size == fp.stat.size &&
        current_fp_stat.mode == fp.stat.mode) {
      // If the file size or mode would have been different then we would have
      // already known for sure that the file is different, but now they are the
      // same. In order to know if it's dirty or not, we need to hash the file
      // again.
      detail::computeFingerprintHash(file_system, fp.stat.mode, path, hash);
      result.clean = fingerprintMatches(fp, current_fp_stat, *hash);

      // At this point, the fingerprint in the invocation log should be
      // re-calculated to avoid this expensive file content check in the future.
      result.should_update = true;
    }
  }
  return result;
}

/**
 * Given a Fingerprint::Stat of a Fingerprint and the time when the fingerprint
 * was taken, compute if the fingerprint is clean or not.
 */
bool isRacilyClean(const Fingerprint::Stat &stat, time_t timestamp) {
  return stat.mtime >= timestamp;
}

}  // anonymous namespace

namespace detail {

void computeFingerprintHash(
    FileSystem &file_system,
    mode_t mode,
    nt_string_view path,
    Hash *hash) throw(IoError) {
  std::string err;
  IoError error;
  if (S_ISDIR(mode)) {
    std::tie(*hash, error) = file_system.hashDir(path);
  } else if (S_ISLNK(mode)) {
    std::tie(*hash, error) = file_system.hashSymlink(path);
  } else if (S_ISREG(mode)) {
    std::tie(*hash, error) = file_system.hashFile(path);
  } else {
    std::fill(hash->data.begin(), hash->data.end(), 0);
  }

  if (error) {
    throw IoError(
        "Could not fingerprint " + std::string(path) + ": " + error.what(),
        error.code());
  }
}

}  // namespace detail

Fingerprint::Stat::Stat() {}

void Fingerprint::Stat::fromStat(const ::shk::Stat &stat, Stat *out) {
  if (stat.result == 0) {
    out->size = stat.metadata.size;
    out->ino = stat.metadata.ino;

    static constexpr auto kDefaultBits =
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;  // 0644
    static constexpr auto kExecutableBits =
        S_IXUSR | S_IXGRP | S_IXOTH;  // 0111
    static constexpr auto kModeMask =
        S_IFMT | S_ISUID | S_ISGID;

    // Like Git, Fingerprints only keep track of either 0755 or 0644 (executable
    // or not) file permissions.
    //
    // In addition to plain file permissions, the type of file (directory vs
    // regular etc, S_IFMT) is tracked, along with setuid and setgid bits.
    const bool executable = !!(stat.metadata.mode & S_IXUSR);
    out->mode =
        kDefaultBits |
        (executable ? kExecutableBits : 0) |
        (stat.metadata.mode & kModeMask);
    out->mtime = stat.mtime;
  }
}

bool Fingerprint::Stat::couldAccess() const {
  return mode != 0;
}

bool Fingerprint::Stat::isDir() const {
  return S_ISDIR(mode);
}

bool Fingerprint::Stat::operator==(const Stat &other) const {
  return (
      size == other.size &&
      ino == other.ino &&
      mode == other.mode &&
      mtime == other.mtime);
}

bool Fingerprint::Stat::operator!=(const Stat &other) const {
  return !(*this == other);
}

bool Fingerprint::Stat::operator<(const Stat &other) const {
  return
      std::tie(size, ino, mode, mtime) <
      std::tie(other.size, other.ino, other.mode, other.mtime);
}

bool Fingerprint::operator==(const Fingerprint &other) const {
  return (
      stat == other.stat &&
      racily_clean == other.racily_clean &&
      hash == other.hash);
}

bool Fingerprint::operator!=(const Fingerprint &other) const {
  return !(*this == other);
}

bool Fingerprint::operator<(const Fingerprint &other) const {
  return
      std::tie(stat, racily_clean, hash) <
      std::tie(other.stat, other.racily_clean, other.hash);
}

bool MatchesResult::operator==(const MatchesResult &other) const {
  return
      clean == other.clean &&
      should_update == other.should_update;
}

bool MatchesResult::operator!=(const MatchesResult &other) const {
  return !(*this == other);
}

std::pair<Fingerprint, FileId> takeFingerprint(
    FileSystem &file_system,
    time_t timestamp,
    nt_string_view path) throw(IoError) {
  std::pair<Fingerprint, FileId> ans;
  Fingerprint &fp = ans.first;
  FileId &file_id = ans.second;

  fingerprintStat(file_system, path, &fp.stat, &file_id);
  fp.racily_clean = isRacilyClean(fp.stat, timestamp);
  detail::computeFingerprintHash(file_system, fp.stat.mode, path, &fp.hash);

  return ans;
}

std::pair<Fingerprint, FileId> retakeFingerprint(
    FileSystem &file_system,
    time_t timestamp,
    nt_string_view path,
    const Fingerprint &old_fingerprint) {
  std::pair<Fingerprint, FileId> ans(old_fingerprint, FileId());
  Fingerprint &new_fingerprint = ans.first;
  FileId &file_id = ans.second;

  const auto stat = file_system.lstat(path);
  file_id = FileId(stat);

  const auto result = fingerprintMatches(
      file_system,
      path,
      old_fingerprint,
      stat,
      &new_fingerprint.hash);
  if (result.clean || result.should_update) {
    // result.should_update means that fingerprintMatches actually had to hash
    // the file to find out if it was clean or not, which means it has set
    // &new_fingerprint.hash, so there is no need to take the fingerprint again,
    // we can just set stat and racily_clean.

    Fingerprint::Stat::fromStat(stat, &new_fingerprint.stat);
    new_fingerprint.racily_clean =
        isRacilyClean(new_fingerprint.stat, timestamp);
    return ans;
  } else {
    return takeFingerprint(file_system, timestamp, path);
  }
}

MatchesResult fingerprintMatches(
    FileSystem &file_system,
    nt_string_view path,
    const Fingerprint &fp) throw(IoError) {
  Hash discard;
  const auto stat = file_system.lstat(path);

  return fingerprintMatches(file_system, path, fp, stat, &discard);
}

bool fingerprintMatches(
    const Fingerprint &original_fingerprint,
    const Fingerprint::Stat &new_stat,
    const Hash &new_hash) {
  return
      original_fingerprint.stat.size == new_stat.size &&
      original_fingerprint.stat.mode == new_stat.mode &&
      original_fingerprint.hash == new_hash;
}

bool fingerprintMatches(
    const Fingerprint &original_fingerprint,
    const Stat &new_stat,
    const Hash &new_hash) {
  Fingerprint::Stat new_fingerprint_stat;
  Fingerprint::Stat::fromStat(new_stat, &new_fingerprint_stat);

  return fingerprintMatches(
      original_fingerprint, new_fingerprint_stat, new_hash);
}

}  // namespace shk
