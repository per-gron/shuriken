#include "fs/fingerprint.h"

#include <sys/stat.h>

namespace shk {
namespace {

void computeFingerprintHash(
    FileSystem &file_system,
    const Fingerprint::Stat &stat,
    nt_string_view path,
    Hash *hash) {
  if (S_ISDIR(stat.mode)) {
    *hash = file_system.hashDir(path);
  } else if (S_ISLNK(stat.mode)) {
    *hash = file_system.hashSymlink(path);
  } else if (stat.couldAccess()) {
    *hash = file_system.hashFile(path);
  } else {
    std::fill(hash->data.begin(), hash->data.end(), 0);
  }
}

bool fingerprintStat(
    FileSystem &file_system,
    nt_string_view path,
    Fingerprint::Stat *out,
    FileId *file_id,
    std::string *err) {
  const auto stat = file_system.lstat(path);
  *file_id = FileId(stat);

  return Fingerprint::Stat::fromStat(stat, out, err);
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
  if (current == fp.stat &&
      (fp.stat.mode == 0 || (
        fp.stat.mtime < fp.timestamp && fp.stat.ctime < fp.timestamp))) {
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
      computeFingerprintHash(file_system, fp.stat, path, hash);
      result.clean = fingerprintMatches(fp, current, *hash);

      // At this point, the fingerprint in the invocation log should be
      // re-calculated to avoid this expensive file content check in the future.
      result.should_update = true;
    }
  }
  return result;
}

}  // anonymous namespace

Fingerprint::Stat::Stat() {}

bool Fingerprint::Stat::fromStat(
    const ::shk::Stat &stat, Stat *out, std::string *err) {
  if (stat.result == 0) {
    out->size = stat.metadata.size;
    out->ino = stat.metadata.ino;
    static constexpr auto mode_mask =
        S_IFMT | S_IRWXU | S_IRWXG | S_IXOTH | S_ISUID | S_ISGID | S_ISVTX;
    out->mode = stat.metadata.mode & mode_mask;
    out->mtime = stat.timestamps.mtime;
    out->ctime = stat.timestamps.ctime;

    if (!S_ISLNK(out->mode) &&
        !S_ISREG(out->mode) &&
        !S_ISDIR(out->mode)) {
      *err =
          "Can only fingerprint regular files, directories and links";
      return false;
    }
  }

  return true;
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
      timestamp == other.timestamp &&
      hash == other.hash);
}

bool Fingerprint::operator!=(const Fingerprint &other) const {
  return !(*this == other);
}

bool Fingerprint::operator<(const Fingerprint &other) const {
  return
      std::tie(stat, timestamp, hash) <
      std::tie(other.stat, other.timestamp, other.hash);
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

  std::string err;
  if (!fingerprintStat(file_system, path, &fp.stat, &file_id, &err)) {
    throw IoError(err, 0);
  }
  fp.timestamp = timestamp;
  computeFingerprintHash(file_system, fp.stat, path, &fp.hash);

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

  std::string err;
  if (!fingerprintStat(file_system, path, &new_fingerprint.stat, &file_id, &err)) {
    throw IoError(err, 0);
  }

  const auto result = fingerprintMatches(
      file_system,
      path,
      old_fingerprint,
      new_fingerprint.stat,
      &new_fingerprint.hash);
  if (result.clean && !result.should_update) {
    return ans;
  } else if (result.should_update) {
    new_fingerprint.timestamp = timestamp;
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

  std::string err;
  Fingerprint::Stat fingerprint_stat;
  if (!fingerprintStat(file_system, path, &fingerprint_stat, &discard2, &err)) {
    throw IoError(err, 0);
  }

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
  std::string err;
  if (!Fingerprint::Stat::fromStat(new_stat, &new_fingerprint_stat, &err)) {
    return false;
  }
  return fingerprintMatches(
      original_fingerprint, new_fingerprint_stat, new_hash);
}

}  // namespace shk
