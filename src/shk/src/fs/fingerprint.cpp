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
    const Fingerprint::Stat &current,
    Hash *hash) throw(IoError) {
  MatchesResult result;
  if (current == fp.stat && (!fp.racily_clean || fp.stat.mode == 0)) {
    // The file's current stat information and the stat information of the
    // fingerprint exactly match. Furthermore, the fingerprint is strictly
    // newer than the files. This means that unless mtime/ctime has been
    // tampered with, we know for sure that the file has not been modified
    // since the fingerprint was taken.
    result.clean = true;
  } else {
    // This branch is hit either when we know for sure that the file has been
    // touched since the fingerprint was taken (current != fp.stat) or when
    // the file is "racily clean" (current == fp.stat but the fingerprint was
    // taken less than one second after the file was last modified).
    //
    // If the file is racily clean, it is not possible to tell if the file
    // matches the fingerprint by looking at stat information only, need to fall
    // back on a file content comparison.
    if (current.size == fp.stat.size && current.mode == fp.stat.mode) {
      // If the file size or mode would have been different then we would have
      // already known for sure that the file is different, but now they are the
      // same. In order to know if it's dirty or not, we need to hash the file
      // again.
      detail::computeFingerprintHash(file_system, fp.stat.mode, path, hash);
      result.clean = fingerprintMatches(fp, current, *hash);

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
  return stat.mtime >= timestamp || stat.ctime >= timestamp;
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
        error.code);
  }
}

}  // namespace detail

Fingerprint::Stat::Stat() {}

void Fingerprint::Stat::fromStat(const ::shk::Stat &stat, Stat *out) {
  if (stat.result == 0) {
    out->size = stat.metadata.size;
    out->ino = stat.metadata.ino;
    static constexpr auto mode_mask =
        S_IFMT | S_IRWXU | S_IRWXG | S_IXOTH | S_ISUID | S_ISGID | S_ISVTX;
    out->mode = stat.metadata.mode & mode_mask;
    out->mtime = stat.timestamps.mtime;
    out->ctime = stat.timestamps.ctime;
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
      mtime == other.mtime &&
      ctime == other.ctime);
}

bool Fingerprint::Stat::operator!=(const Stat &other) const {
  return !(*this == other);
}

bool Fingerprint::Stat::operator<(const Stat &other) const {
  return
      std::tie(size, ino, mode, mtime, ctime) <
      std::tie(other.size, other.ino, other.mode, other.mtime, other.ctime);
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

  fingerprintStat(file_system, path, &new_fingerprint.stat, &file_id);

  const auto result = fingerprintMatches(
      file_system,
      path,
      old_fingerprint,
      new_fingerprint.stat,
      &new_fingerprint.hash);
  if (result.clean) {
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
  Hash discard1;
  FileId discard2;

  Fingerprint::Stat fingerprint_stat;
  fingerprintStat(file_system, path, &fingerprint_stat, &discard2);

  return fingerprintMatches(
      file_system,
      path,
      fp,
      fingerprint_stat,
      &discard1);
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
