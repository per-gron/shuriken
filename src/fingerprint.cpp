#include "fingerprint.h"

namespace shk {

def matches(Fingerprint fp, Path path):
  Stat current = lstat(path)
  if current == fp.stat:
    if fp.stat.timestamps.mtime < fp.timestamp:
      return True
    else:
      # The file is racily clean. This happens when a the fingerprint
      # was taken during the same second as the file was last modified.
      # It is not possible to tell if the file matches the fingerprint
      # by looking at stat information only, need to fall back on a
      # file content comparison.
      #
      # At this point, the fingerprint should be updated to avoid the
      # racily clean state.
      return hash(path) == fp.hash
  else:
    # The file has definitely been touched, but might still have the
    # same contents
    #
    # Might be better to simply return False here and allow redundant
    # rebuilds.
    #
    # At this point, the fingerprint should be updated to avoid the
    # expensive file content check in the future.
    return current.metadata == fp.stat.metadata and hash(path) == fp.hash

}  // namespace shk
