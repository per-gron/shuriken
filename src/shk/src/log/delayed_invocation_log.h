#pragma once

#include "clock.h"
#include "log/invocation_log.h"

namespace shk {

/**
 * DelayedInvocationLog is an invocation log that attempts to minimize racily
 * clean invocation log entries. In short: When an entry is written to the
 * invocation log, it usually contains fingerprints of files. The fingerprints
 * contain a hash of the file along with some extra metadata, for example file
 * size and modification times. On subsequent builds, these fingerprints are
 * used to decide if a file has to be rebuilt or not. In many cases, the
 * fingerprint matching only needs to stat the fingerprinted file to know if it
 * is dirty or not (it is dirty for sure if the file size is different, it is
 * clean for sure if the time the fingerprint was taken is strictly newer than
 * the file's last modification date). However, if the file size is the same and
 * the fingerprint was taken the same second as the mtime of the file, the
 * fingerprint matcher has to hash the contents of the file to decide if the
 * file has been changed or not. This is a fairly expensive operation, so the
 * system attempts to avoid it.
 *
 * One important way to avoiding it is that the build system will write a new
 * invocation log entry every time it has to process an entry that requires
 * hashing of the file. This usually avoids the need of doing it in the future.
 *
 * Even with that optimization, there is a fairly severe problem still left
 * unfixed: Output files of build steps are almost always created the same
 * second as the fingerprint is taken. This makes it so that when doing a clean
 * build, each output file is hashed immediately when built, and then on the
 * next build, every output file has to be hashed again. This causes that second
 * build that the user expects to be a quick no-op build to take quite some time
 * to perform.
 *
 * DelayedInvocationLog is here to avoid this problem. It does so by delaying
 * logging of commands until the next second, except in the very end of the
 * build, where all remaining things are written out immediately.
 *
 * See MatchesResult::should_update in fingerprint.h
 */
std::unique_ptr<InvocationLog> delayedInvocationLog(
    const Clock &clock,
    std::unique_ptr<InvocationLog> &&inner_log);

}  // namespace shk
