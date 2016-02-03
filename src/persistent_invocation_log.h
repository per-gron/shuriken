#pragma once

#include "invocation_log.h"
#include "invocations.h"

#include <stdint.h>

namespace shk {
namespace detail {

/**
 * This file provides functions for reading and manipulating the on-disk
 * representation of the invocation log. The purpose of the invocation log is
 * to keep track of commands that have been run before, to be able to calculate
 * if a command needs to be re-run or not, and to be able to clean up output
 * files when necessary.
 *
 * The invocation log of Shuriken is similar to a combination of the deps log
 * and the build log of Ninja. Like Ninja's build log, it contains an entry for
 * every command that Shuriken has run. Like the deps log, it contains
 * information about dependencies that were gathered during previous builds.
 *
 * In Shuriken it does not make sense to keep those logs separate, because
 * unlike Ninja, Shuriken tracks dependencies of every build step. Ninja only
 * uses the deps log for rules that have a depsfile.
 *
 * The format of the invocation log is similar to that of Ninja's deps log, but
 * it is slightly more compilcated because Shuriken also tracks directories that
 * it has created.
 *
 * Like Ninja's deps log, the invocation log has to support the following use
 * cases:
 *
 * 1. It needs to support writing to in a streaming way, as commands are being
 *    run. This is important to support interrupted builds.
 * 2. It needs to be read all at once on startup.
 *
 * The invocation log is a single binary file. Its contents are dependent on the
 * endianness of the machine, so invocation log files are not always portable
 * between machines. It contains a version header followed by a series of
 * entries. An entry consists of a uint32_t of the entry size where the two
 * least significant bits signify the entry type followed by entry type specific
 * contents. Each entry is implicitly assigned an identifier. The first entry
 * has id 0, the second has id 1 and so on.
 *
 * There are four types of entries:
 *
 * 0. Path: The contents is a single string with a path.
 * 1. Created directory: The contents is a single uint32_t entry id reference to
 *    a path of the created directory.
 * 2. Invocation: An Invocation entry is an on-disk representation of an
 *    Invocations::Entry object. It starts with a single uint32_t with the
 *    number of output files, followed by a number of
 *    [uint32_t entry id, Fingerprint] pairs. The first pairs (the count
 *    specified in the first uint32_t) are outputs, the rest are inputs.
 * 3. Deleted entry: The contents is a single uint32_t entry id reference to a
 *    created directory or invocation. It means that the referenced directory
 *    has been deleted or that the invocation has been cleaned, so Shuriken
 *    should act as if the entry does not exist.
 *
 * The invocation log is designed to be used by only one process at a time. The
 * processing functions here assume that the user of these functions has somehow
 * acquired exclusive access to the invocation log file.
 */

enum class InvocationLogEntryType : uint32_t {
  PATH = 0,
  CREATED_DIR = 1,
  INVOCATION = 2,
  DELETED = 3,
};

}  // namespace detail

/**
 * A map of Path objects to the record id in the invocation log. This object is
 * produced when parsing the invocation log and used when writing to the
 * invocation log, to avoid duplication of paths in the log.
 */
using PathIds = std::unordered_map<Path, uint32_t>;

struct InvocationLogParseResult {
  Invocations invocations;
  /**
   * If non-empty, the function that parsed the invocation logs wants to warn
   * the user about something. This is the warning message.
   */
  std::string warning;
  bool needs_recompaction;
  PathIds path_ids;
};

/**
 * Parse an invocation log at a given path into an Invocations object.
 *
 * A missing invocation log file does not count as an error. This simply causes
 * this function to return an empty Invocations object.
 *
 * The invocation log is designed to be used by only one process at a time. This
 * function assumes that the user of these functions has somehow acquired
 * exclusive access to the invocation log file.
 */
InvocationLogParseResult parsePersistentInvocationLog(
    FileSystem &file_system,
    const std::string &log_path) throw(IoError);

/**
 * Create a disk-backed InvocationLog. This is the main InvocationLog
 * implementation. The InvocationLog object provided here (like all other such
 * objects) only provide means to write to the invocation log. Reading happens
 * before, in a separate step.
 *
 * The invocation log is designed to be used by only one process at a time. This
 * function assumes that the user of these functions has somehow acquired
 * exclusive access to the invocation log file.
 */
std::unique_ptr<InvocationLog> openPersistentInvocationLog(
    FileSystem &file_system,
    const std::string &log_path,
    PathIds &&path_ids) throw(IoError);

/**
 * Overwrite the invocation log file with a new one that contains only the
 * entries of invocations. This invalidates any open persistent InvocationLog
 * object to this path: The old invocation log file is unlinked.
 *
 * The invocation log is designed to be used by only one process at a time. This
 * function assumes that the user of these functions has somehow acquired
 * exclusive access to the invocation log file.
 */
void recompactPersistentInvocationLog(
    FileSystem &file_system,
    const Invocations &invocations,
    const std::string &log_path) throw(IoError);

}  // namespace shk
