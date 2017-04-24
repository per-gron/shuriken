#pragma once

#include <stdint.h>

#include "clock.h"
#include "fs/file_system.h"
#include "log/invocation_log.h"
#include "log/invocations.h"
#include "parse_error.h"

namespace shk {

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
 * entries. An entry consists of a uint32_t of the entry size (excluding the
 * header) where the two least significant bits signify the entry type followed
 * by entry type specific contents.
 *
 * Each entry is implicitly assigned an identifier, depending on its type. The
 * first Fingerprint entry has id 0, the first Path entry has id 1, the second
 * Fingerprint entry has id 1 and so on. The types with separate identifier
 * sequences are Path and Fingerprint. The other entry types are not referred to
 * by id.
 *
 * There are four types of entries:
 *
 * 0. Path: The contents is a single null-terminated string with a path,
 *    possibly with extra trailing \0s to ensure 4 byte alignment.
 * 1. Created directory or Fingerprint: If the size is 4 bytes, the contents is
 *    a single uint32_t entry id reference to a path of the created directory.
 *    Otherwise, this entry contains a uint32_t entry id reference to a path of
 *    a fingerprinted file followed by a Fingerprint object for that path (with
 *    no relation to directories).
 * 2. Invocation: An Invocation entry is an on-disk representation of an
 *    Invocations::Entry object. It starts with a Hash object, then contains a
 *    single uint32_t with the number of output files, followed by a number of
 *    uint32_t fingerprint entry ids. The first fingerprint ids are outputs, the
 *    rest are inputs.
 * 3. Deleted entry: If the size is 4 bytes, the contents is a single uint32_t
 *    path id reference to a directory that has been deleted. If the size is
 *    sizeof(Hash), it contains a hash of an Invocations::Entry that has been
 *    deleted. When seeing a deleted entry, Shuriken acts as if the deleted
 *    entry does not exist in the log.
 *
 * Whenever an entry refers to another entry by id, the entry referred to must
 * have a lower id than the entry that refers to it.
 *
 * The invocation log is designed to be used by only one process at a time. The
 * processing functions here assume that the user of these functions has somehow
 * acquired exclusive access to the invocation log file.
 */

/**
 * A map of paths to the record id in the invocation log. This object is
 * produced when parsing the invocation log and used when writing to the
 * invocation log, to avoid duplication of paths in the log.
 */
using PathIds = std::unordered_map<std::string, uint32_t>;
/**
 * A map of paths to the record id of the most recent fingerprint for that path
 * in the invocation log. In order to be able to decide if the most recent
 * fingerprint can be used or not, this map also contains the Fingerprint
 * itself. Like PathIds, this object is produced when parsing the invocation log
 * and used when writing to the invocation log, to avoid duplication in the log,
 * and to avoid unnecessary re-hashing of file contents.
 */
struct FingerprintIdsValue {
  uint32_t record_id = 0;
  Fingerprint fingerprint;
};
using FingerprintIds = std::unordered_map<
    std::string, FingerprintIdsValue>;

struct InvocationLogParseResult {
  /**
   * Struct containing information that is needed when opening an invocation log
   * for writing. Users of this API should not directly inspect objects of this
   * class.
   */
  struct ParseData {
    PathIds path_ids;
    FingerprintIds fingerprint_ids;
    uint32_t fingerprint_entry_count = 0;
    uint32_t path_entry_count = 0;
  };

  Invocations invocations;
  /**
   * If non-empty, the function that parsed the invocation logs wants to warn
   * the user about something. This is the warning message.
   */
  std::string warning;
  bool needs_recompaction = false;
  ParseData parse_data;
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
 *
 * Parsing the invocation log is not necessarily a purely read-only action: If
 * an invalid entry is encountered, the invocation log is truncated to just
 * before that entry.
 */
InvocationLogParseResult parsePersistentInvocationLog(
    FileSystem &file_system,
    const std::string &log_path) throw(IoError, ParseError);

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
    const Clock &clock,
    const std::string &log_path,
    InvocationLogParseResult::ParseData &&parse_data) throw(IoError);

/**
 * Overwrite the invocation log file with a new one that contains only the
 * entries of invocations. This invalidates any open persistent InvocationLog
 * object to this path: The old invocation log file is unlinked.
 *
 * The invocation log is designed to be used by only one process at a time. This
 * function assumes that the user of these functions has somehow acquired
 * exclusive access to the invocation log file.
 *
 * After recompacting the invocation log, any previous ParseData object from
 * parseInvocationLog is invalid. Instead, use the return value of this
 * function.
 */
InvocationLogParseResult::ParseData recompactPersistentInvocationLog(
    FileSystem &file_system,
    const Clock &clock,
    const Invocations &invocations,
    const std::string &log_path) throw(IoError);

}  // namespace shk
