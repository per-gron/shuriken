#include "fingerprint.h"

#include <sys/stat.h>

namespace shk {
namespace {

Fingerprint::Stat fingerprintStat(
    FileSystem &file_system, const std::string &path) throw(IoError) {
  Fingerprint::Stat result;

  // TODO(peck): It would not be correct to use lstat here because then we could
  // report that the file is a symlink even though it points to a directory,
  // which later on causes the build to fail when it attempts to hash the
  // directory that is pointed to as if it were a file. Perhaps a solution to
  // this could be to hash symlinks in a special way.
  const auto stat = file_system.stat(path);
  if (stat.result == 0) {
    result.size = stat.metadata.size;
    result.ino = stat.metadata.ino;
    static constexpr auto mode_mask =
        S_IFMT | S_IRWXU | S_IRWXG | S_IXOTH | S_ISUID | S_ISGID | S_ISVTX;
    result.mode = stat.metadata.mode & mode_mask;
    result.mtime = stat.timestamps.mtime;
    result.ctime = stat.timestamps.ctime;

    if (!S_ISLNK(result.mode) &&
        !S_ISREG(result.mode) &&
        !S_ISDIR(result.mode)) {
      throw IoError(
          "Can only fingerprint regular files, directories and links: " + path, 0);
    }
  }

  return result;
}

/**
 * fingerprintMatches logic that is shared between fingerprintMatches and
 * retakeFingerprint. If the return value MatchesResult::clean is true, then
 * the hash output parameter is set to the hash value of the file that the
 * function had to compute to detect if it was clean or not.
 */
MatchesResult fingerprintMatches(
    FileSystem &file_system,
    const std::string &path,
    const Fingerprint &fp,
    const Fingerprint::Stat &current,
    Hash &hash) throw(IoError) {
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
      if (S_ISDIR(fp.stat.mode)) {
        hash = file_system.hashDir(path);
      } else {
        hash = file_system.hashFile(path);
      }
      result.clean = (hash == fp.hash);

      // At this point, the fingerprint in the invocation log should be
      // re-calculated to avoid this expensive file content check in the future.
      result.should_update = true;
    }
  }
  return result;
}

}  // anonymous namespace

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

Fingerprint takeFingerprint(
    FileSystem &file_system,
    time_t timestamp,
    const std::string &path) throw(IoError) {
  Fingerprint fp;

  fp.stat = fingerprintStat(file_system, path);
  fp.timestamp = timestamp;
  if (S_ISDIR(fp.stat.mode)) {
    fp.hash = file_system.hashDir(path);
  } else if (fp.stat.couldAccess()) {
    fp.hash = file_system.hashFile(path);
  } else {
    std::fill(fp.hash.data.begin(), fp.hash.data.end(), 0);
  }

  return fp;
}

Fingerprint retakeFingerprint(
    FileSystem &file_system,
    time_t timestamp,
    const std::string &path,
    const Fingerprint &old_fingerprint) {
  Fingerprint new_fingerprint;
  new_fingerprint.timestamp = timestamp;
  new_fingerprint.stat = fingerprintStat(file_system, path);
  const auto result = fingerprintMatches(
      file_system,
      path,
      old_fingerprint,
      new_fingerprint.stat,
      new_fingerprint.hash);
  if (result.clean && !result.should_update) {
    return old_fingerprint;
  } else if (result.should_update) {
    // new_fingerprint.hash has been set by fingerprintMatches
    return new_fingerprint;
  } else {
    return takeFingerprint(file_system, timestamp, path);
  }
}

MatchesResult fingerprintMatches(
    FileSystem &file_system,
    const std::string &path,
    const Fingerprint &fp) throw(IoError) {
  Hash discard;
  return fingerprintMatches(
      file_system,
      path,
      fp,
      fingerprintStat(file_system, path),
      discard);
}

}  // namespace shk
