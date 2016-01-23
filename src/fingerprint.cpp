#include "fingerprint.h"

#include <sys/stat.h>

namespace shk {
namespace {

Fingerprint::Stat fingerprintStat(
    FileSystem &file_system, const std::string &path) throw(IoError) {
  Fingerprint::Stat result;

  const auto stat = file_system.lstat(path);
  if (stat.result == 0) {
    result.size = stat.metadata.size;
    result.ino = stat.metadata.ino;
    static constexpr auto mode_mask =
        S_IFMT | S_IRWXU | S_IRWXG | S_IXOTH | S_ISUID | S_ISGID | S_ISVTX;
    result.mode = stat.metadata.mode & mode_mask;
    result.mtime = stat.timestamps.mtime;
    result.ctime = stat.timestamps.ctime;

    if (!S_ISLNK(result.mode) && !S_ISREG(result.mode) && !S_ISDIR(result.mode)) {
      throw IoError(
          "Can only fingerprint regular files, directories and links", 0);
    }
  }

  return result;
}

}  // anonymous namespace

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

MatchesResult fingerprintMatches(
    FileSystem &file_system,
    const std::string &path,
    const Fingerprint &fp) throw(IoError) {
  MatchesResult result;
  const auto current = fingerprintStat(file_system, path);
  if (current == fp.stat &&
      fp.stat.mtime < fp.timestamp && fp.stat.ctime < fp.timestamp) {
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
        result.clean = file_system.hashDir(path) == fp.hash;
      } else {
        result.clean = file_system.hashFile(path) == fp.hash;
      }

      // At this point, the fingerprint in the invocation log should be
      // re-calculated to avoid this expensive file content check in the future.
      result.should_update = true;
    }
  }
  return result;
}

}  // namespace shk
