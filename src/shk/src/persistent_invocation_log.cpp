// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "persistent_invocation_log.h"

#include <assert.h>
#include <errno.h>

#include "optional.h"

namespace shk {
namespace {

enum class InvocationLogEntryType : uint32_t {
  PATH = 0,
  CREATED_DIR_OR_FINGERPRINT = 1,
  INVOCATION = 2,
  DELETED = 3,
};

const std::string kFileSignature = "invocations:";
const uint32_t kFileVersion = 1;
const uint32_t kInvocationLogEntryTypeMask = 3;

StringPiece advance(StringPiece piece, size_t len) {
  assert(len <= piece._len);
  return StringPiece(piece._str + len, piece._len - len);
}

StringPiece parseInvocationLogSignature(StringPiece piece) throw(ParseError) {
  const auto signature_size = kFileSignature.size() + sizeof(kFileVersion);
  if (piece._len < signature_size) {
    throw ParseError("invalid invocation log file signature (too short)");
  }

  if (!std::equal(kFileSignature.begin(), kFileSignature.end(), piece._str)) {
    throw ParseError("invalid invocation log file signature");
  }

  const auto version =
      *reinterpret_cast<const uint32_t *>(piece._str + kFileSignature.size());
  if (version != kFileVersion) {
    throw ParseError("invalid invocation log file version or bad byte order");
  }

  return advance(piece, signature_size);
}

class EntryHeader {
 public:
  using Value = uint32_t;

  EntryHeader(StringPiece piece) throw(ParseError) {
    if (piece._len < sizeof(Value)) {
      throw ParseError("invalid invocation log: encountered truncated entry");
    }

    _header = *reinterpret_cast<const uint32_t *>(piece._str);
  }

  uint32_t entrySize() const {
    return _header & ~kInvocationLogEntryTypeMask;
  }

  InvocationLogEntryType entryType() const {
    return static_cast<InvocationLogEntryType>(
        _header & kInvocationLogEntryTypeMask);
  }

 private:
  Value _header;
};

void ensureEntryLen(
    const StringPiece &piece, size_t min_size) throw(ParseError) {
  if (piece._len < min_size) {
    throw ParseError("invalid invocation log: encountered invalid entry");
  }
}

template<typename T>
const T &read(const StringPiece &piece) {
  ensureEntryLen(piece, sizeof(T));
  return *reinterpret_cast<const T *>(piece._str);
}

template<typename T>
T readEntryById(
    const std::vector<Optional<T>> &entries_by_id,
    StringPiece piece) throw(ParseError) {
  const auto entry_id = read<uint32_t>(piece);
  if (entry_id >= entries_by_id.size() || !entries_by_id[entry_id]) {
    throw ParseError(
        "invalid invocation log: encountered invalid fingerprint ref");
  }
  return *entries_by_id[entry_id];
}

std::vector<size_t> readFingerprints(
    const std::vector<Optional<size_t>> &fingerprints_by_id,
    StringPiece piece) {
  std::vector<size_t> result;
  result.reserve(piece._len / sizeof(uint32_t));

  for (; piece._len; piece = advance(piece, sizeof(uint32_t))) {
    result.push_back(readEntryById(fingerprints_by_id, piece));
  }

  return result;
}

Path readPath(
    const std::vector<Optional<Path>> &paths_by_id,
    StringPiece piece) throw(ParseError) {
  const auto path_id = read<uint32_t>(piece);
  if (path_id >= paths_by_id.size() || !paths_by_id[path_id]) {
    throw ParseError(
        "invalid invocation log: encountered invalid path ref");
  }
  return *paths_by_id[path_id];
}

class PersistentInvocationLog : public InvocationLog {
 public:
  PersistentInvocationLog(
      FileSystem &file_system,
      const Clock &clock,
      std::unique_ptr<FileSystem::Stream> &&stream,
      InvocationLogParseResult::ParseData &&parse_data)
      : _fs(file_system),
        _clock(clock),
        _stream(std::move(stream)),
        _path_ids(std::move(parse_data.path_ids)),
        _fingerprint_ids(std::move(parse_data.fingerprint_ids)),
        _entry_count(parse_data.entry_count) {
    writeHeader();
  }

  void createdDirectory(const std::string &path) throw(IoError) override {
    writeDirectoryEntry(ensurePathIsWritten(path));
  }

  void removedDirectory(const std::string &path) throw(IoError) override {
    const auto it = _path_ids.find(path);
    if (it == _path_ids.end()) {
      // The directory has not been created so it can't be removed.
      return;
    }
    writeHeader(sizeof(uint32_t), InvocationLogEntryType::DELETED);
    write(it->second);
    _entry_count++;
  }

  void ranCommand(
      const Hash &build_step_hash,
      std::unordered_set<std::string> &&output_files,
      std::unordered_map<std::string, DependencyType> &&input_files_map)
          throw(IoError) override {

    output_files = writeOutputPathsAndFingerprints(std::move(output_files));
    const auto input_files = writeInputPathsAndFingerprints(
        std::move(input_files_map));

    const auto total_entries = output_files.size() + input_files.size();
    const uint32_t size =
        sizeof(Hash) +
        sizeof(uint32_t) +
        sizeof(uint32_t) * total_entries;

    writeHeader(size, InvocationLogEntryType::INVOCATION);

    write(build_step_hash);
    write(static_cast<uint32_t>(output_files.size()));

    writeFiles(output_files);
    writeFiles(input_files);

    _entry_count++;
  }

  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override {
    writeHeader(sizeof(Hash), InvocationLogEntryType::DELETED);
    write(build_step_hash);
    _entry_count++;
  }

 private:
  void writeFiles(const std::unordered_set<std::string> &paths) {
    for (const auto &path : paths) {
      const auto fingerprint_it = _fingerprint_ids.find(path);
      assert(fingerprint_it != _fingerprint_ids.end());
      write(fingerprint_it->second.record_id);
    }
  }

  /**
   * Used for output files.
   */
  std::unordered_set<std::string> writeOutputPathsAndFingerprints(
      std::unordered_set<std::string> &&paths) {
    std::unordered_set<std::string> result;
    for (auto &path : paths) {
      const auto fingerprint = ensureRecentFingerprintIsWritten(
          path, WriteType::DIRECTORY_AS_DIRECTORY_ENTRY);
      if (!fingerprint.stat.isDir()) {
        result.insert(std::move(path));
      }
    }
    return result;
  }

  std::unordered_set<std::string> writeInputPathsAndFingerprints(
      const std::unordered_map<std::string, DependencyType> &dependencies) {
    std::unordered_set<std::string> result;
    for (auto &&dep : dependencies) {
      const auto fingerprint = ensureRecentFingerprintIsWritten(
          dep.first, WriteType::ALWAYS_FINGERPRINT);
      if (!fingerprint.stat.isDir() || dep.second == DependencyType::ALWAYS) {
        result.insert(std::move(dep.first));
      }
    }
    return result;
  }

  void writeHeader() {
    if (_stream->tell() == 0) {
      _stream->write(
          reinterpret_cast<const uint8_t *>(kFileSignature.data()),
          kFileSignature.size(),
          1);
      // The file version implicitly serves as a byte order mark
      write(kFileVersion);
    }
  }

  template<typename T>
  void write(const T &val) {
    _stream->write(reinterpret_cast<const uint8_t *>(&val), sizeof(val), 1);
  }

  void writeHeader(size_t size, InvocationLogEntryType type) {
    assert((size & kInvocationLogEntryTypeMask) == 0);
    write(static_cast<uint32_t>(size) | static_cast<uint32_t>(type));
  }

  void writePathEntry(const std::string &path) {
    const auto path_size = path.size() + 1;
    const auto padding_bytes =
        (4 - (path_size & kInvocationLogEntryTypeMask)) % 4;
    writeHeader(
        path_size + padding_bytes,
        InvocationLogEntryType::PATH);

    _stream->write(
        reinterpret_cast<const uint8_t *>(path.data()), path_size, 1);

    // Ensure that the output is 4-byte aligned.
    static const char *kNullBuf = "\0\0\0";
    _stream->write(
        reinterpret_cast<const uint8_t *>(kNullBuf), padding_bytes, 1);

    _entry_count++;
  }

  void writeDirectoryEntry(const uint32_t path_id) {
    writeHeader(
        sizeof(uint32_t), InvocationLogEntryType::CREATED_DIR_OR_FINGERPRINT);
    write(path_id);
    _entry_count++;
  }

  void writeFingerprintEntry(
        const uint32_t path_id, const Fingerprint &fingerprint) {
    writeHeader(
        sizeof(path_id) + sizeof(fingerprint),
        InvocationLogEntryType::CREATED_DIR_OR_FINGERPRINT);

    write(path_id);
    _stream->write(
        reinterpret_cast<const uint8_t *>(&fingerprint), sizeof(fingerprint), 1);

    _entry_count++;
  }

  /**
   * Get the id for a path. If the path is not already written, write an entry
   * with that path. This means that this method cannot be called in the middle
   * of writing another entry.
   */
  uint32_t ensurePathIsWritten(const std::string &path) {
    const auto it = _path_ids.find(path);
    if (it == _path_ids.end()) {
      const auto id = _entry_count;
      writePathEntry(path);
      _path_ids[path] = id;
      return id;
    } else {
      return it->second;
    }
  }

  enum class WriteType {
    ALWAYS_FINGERPRINT,
    DIRECTORY_AS_DIRECTORY_ENTRY
  };

  void writeFingerprintOrDirectoryEntry(
        const uint32_t path_id,
        const Fingerprint &fingerprint,
        WriteType type) {
    if (type == WriteType::DIRECTORY_AS_DIRECTORY_ENTRY &&
        fingerprint.stat.isDir()) {
      writeDirectoryEntry(path_id);
    } else {
      writeFingerprintEntry(path_id, fingerprint);
    }
  }

  /**
   * Get the id for a fingerprint of a path. If there is no fingerprint written
   * for the given path, just take the fingerprint. If there is a fingerprint
   * already for that path, attempt to reuse it, in an effort to avoid
   * re-hashing the file. This is important for performance, because otherwise
   * the build would hash every input file for every build step, including files
   * that are used for every build step, such as system libraries.
   *
   * Returns the fingerprint for the given path.
   *
   * Because this might write an entry to the log, this method cannot be called
   * in the middle of writing another entry.
   */
  Fingerprint ensureRecentFingerprintIsWritten(
        const std::string &path, WriteType type) {
    const auto path_id = ensurePathIsWritten(path);

    FingerprintIdsValue value;
    value.record_id = _entry_count;
    const auto it = _fingerprint_ids.find(path);
    if (it == _fingerprint_ids.end()) {
      // No prior entry for that path. Need to take fingerprint.
      value.fingerprint = takeFingerprint(_fs, _clock(), path);
      writeFingerprintOrDirectoryEntry(path_id, value.fingerprint, type);
      _fingerprint_ids[path] = value;
      return value.fingerprint;
    } else {
      // There is a fingerprint entry for the given path already. Find out if it
      // can be reused or if a new fingerprint is required.
      const auto &old_fingerprint = it->second.fingerprint;
      value.fingerprint = retakeFingerprint(
          _fs, _clock(), path, old_fingerprint);
      if (old_fingerprint == value.fingerprint) {
        return it->second.fingerprint;
      } else {
        writeFingerprintOrDirectoryEntry(path_id, value.fingerprint, type);
        _fingerprint_ids[path] = value;
        return value.fingerprint;
      }
    }
  }

  FileSystem &_fs;
  const Clock _clock;
  const std::unique_ptr<FileSystem::Stream> _stream;
  PathIds _path_ids;
  FingerprintIds _fingerprint_ids;
  size_t _entry_count;
};

}

InvocationLogParseResult parsePersistentInvocationLog(
    Paths &paths,
    FileSystem &file_system,
    const std::string &log_path) throw(IoError, ParseError) {
  InvocationLogParseResult result;

  std::unique_ptr<FileSystem::Mmap> mmap;
  try {
    mmap = file_system.mmap(log_path);
  } catch (IoError &io_error) {
    if (io_error.code == ENOENT) {
      return result;
    } else {
      throw;
    }
  }
  auto piece = mmap->memory();
  const auto file_size = piece._len;

  piece = parseInvocationLogSignature(piece);

  // "Map" from path entry id to path. Entries that aren't path entries are
  // empty.
  std::vector<Optional<Path>> paths_by_id;
  // "Map" from fingerprint entry id to index in Invocations::fingerprints.
  // Indices that refer to entries that have been read but arent fingerprint
  // entries are empty.
  std::vector<Optional<size_t>> fingerprints_by_id;

  auto &entry_count = result.parse_data.entry_count;

  try {
    for (; piece._len; entry_count++) {
      EntryHeader header(piece);
      const auto entry_size = header.entrySize();
      ensureEntryLen(piece, entry_size + sizeof(EntryHeader::Value));
      StringPiece entry(piece._str + sizeof(EntryHeader::Value), entry_size);

      switch (header.entryType()) {
      case InvocationLogEntryType::PATH: {
        paths_by_id.resize(entry_count + 1);
        if (strnlen(entry._str, entry._len) == entry._len) {
          throw ParseError(
              "invalid invocation log: Encountered non null terminated path");
        }
        // Don't use entry.asString() because it could make the string contain
        // trailing \0s
        auto path_string = std::string(entry._str);
        result.parse_data.path_ids[path_string] = entry_count;
        paths_by_id[entry_count] = paths.get(std::move(path_string));
        break;
      }

      case InvocationLogEntryType::CREATED_DIR_OR_FINGERPRINT: {
        if (entry_size == sizeof(uint32_t)) {
          const auto path = readPath(paths_by_id, entry);
          path.fileId().each([&](const FileId file_id) {
            // Only add the directory to the resulting Invocations object if the
            // file exists. For more info see Invocations::created_directories
            result.invocations.created_directories.emplace(file_id, path);
          });
        } else if (entry_size == sizeof(uint32_t) + sizeof(Fingerprint)) {
          const auto path = readPath(paths_by_id, entry);
          entry = advance(entry, sizeof(uint32_t));

          FingerprintIdsValue value;
          value.record_id = entry_count;
          value.fingerprint =
              *reinterpret_cast<const Fingerprint *>(entry._str);
          result.parse_data.fingerprint_ids[path.original()] = value;

          fingerprints_by_id.resize(entry_count + 1);
          fingerprints_by_id[entry_count] =
              result.invocations.fingerprints.size();
          result.invocations.fingerprints.emplace_back(
              path, value.fingerprint);
        } else {
          throw ParseError("invalid invocation log: truncated invocation");
        }
        break;
      }

      case InvocationLogEntryType::INVOCATION: {
        const auto hash = read<Hash>(entry);
        entry = advance(entry, sizeof(hash));
        const auto outputs = read<uint32_t>(entry);
        entry = advance(entry, sizeof(outputs));
        const auto output_size = sizeof(uint32_t) * outputs;
        if (entry._len < output_size) {
          throw ParseError("invalid invocation log: truncated invocation");
        }

        result.invocations.entries[hash] = {
            readFingerprints(
                fingerprints_by_id,
                StringPiece(entry._str, output_size)),
            readFingerprints(
                fingerprints_by_id,
                advance(entry, output_size)) };
        break;
      }

      case InvocationLogEntryType::DELETED: {
        if (entry._len == sizeof(uint32_t)) {
          // Deleted directory
          const auto path = readPath(paths_by_id, entry);
          path.fileId().each([&](const FileId file_id) {
            result.invocations.created_directories.erase(file_id);
          });
        } else if (entry._len == sizeof(Hash)) {
          // Deleted invocation
          result.invocations.entries.erase(read<Hash>(entry));
        } else {
          throw ParseError("invalid invocation log: invalid deleted entry");
        }
        break;
      }
      }

      // Now that we are sure that the parsing succeeded, advance piece. This
      // is important because the truncation logic below depends on piece
      // pointing to the end of a valid entry.
      piece = advance(piece, sizeof(EntryHeader::Value) + entry_size);
    }
  } catch (PathError &error) {
    // Parse error while parsing the invocation log. Treat this as a warning and
    // truncate the invocation log to the last known valid entry.
    result.warning =
        std::string("encountered invalid path in invocation log: ") +
        error.what();
  } catch (ParseError &error) {
    // Parse error while parsing the invocation log. Treat this as a warning and
    // truncate the invocation log to the last known valid entry.
    result.warning = error.what();
  }

  if (piece._len != 0) {
    // Parsing failed. Truncate the file to a known valid state
    file_system.truncate(log_path, file_size - piece._len);
  }

  // Rebuild the log if there are too many dead records.
  int kMinCompactionEntryCount = 1000;
  int kCompactionRatio = 3;
  const auto unique_record_count =
      result.invocations.entries.size() +
      result.invocations.created_directories.size() +
      result.parse_data.path_ids.size();
  result.needs_recompaction = (
      entry_count > kMinCompactionEntryCount &&
      entry_count > unique_record_count * kCompactionRatio);

  return result;
}

std::unique_ptr<InvocationLog> openPersistentInvocationLog(
    FileSystem &file_system,
    const Clock &clock,
    const std::string &log_path,
    InvocationLogParseResult::ParseData &&parse_data) throw(IoError) {
  return std::unique_ptr<InvocationLog>(
      new PersistentInvocationLog(
          file_system,
          clock,
          file_system.open(log_path, "ab"),
          std::move(parse_data)));
}

void recompactPersistentInvocationLog(
    FileSystem &file_system,
    const Clock &clock,
    const Invocations &invocations,
    const std::string &log_path) throw(IoError) {
  const auto tmp_path = file_system.mkstemp("shk.tmp.log.XXXXXXXX");
  const auto log =
      openPersistentInvocationLog(
          file_system, clock, tmp_path, InvocationLogParseResult::ParseData());

  for (const auto &dir : invocations.created_directories) {
    log->createdDirectory(dir.second.original());
  }

  for (const auto &entry : invocations.entries) {
    log->relogCommand(
        entry.first,
        invocations.fingerprints,
        entry.second.output_files,
        entry.second.input_files);
  }

  file_system.rename(tmp_path, log_path);
}

}  // namespace shk
