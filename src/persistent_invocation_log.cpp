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
  CREATED_DIR = 1,
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

std::vector<std::pair<Path, Fingerprint>> readFingerprints(
    const std::vector<Optional<Path>> &paths_by_id,
    StringPiece piece) {
  std::vector<std::pair<Path, Fingerprint>> result;

  while (piece._len) {
    const auto path = readPath(paths_by_id, piece);
    piece = advance(piece, sizeof(uint32_t));
    result.emplace_back(path, read<Fingerprint>(piece));
    piece = advance(piece, sizeof(Fingerprint));
  }

  return result;
}

class PersistentInvocationLog : public InvocationLog {
 public:
  PersistentInvocationLog(
      std::unique_ptr<FileSystem::Stream> &&stream,
      InvocationLogParseResult::ParseData &&parse_data)
      : _stream(std::move(stream)),
        _path_ids(std::move(parse_data.path_ids)),
        _fingerprint_ids(std::move(parse_data.fingerprint_ids)),
        _entry_count(parse_data.entry_count) {
    writeHeader();
  }

  void createdDirectory(const std::string &path) throw(IoError) override {
    const auto path_id = idForPath(path);
    writeHeader(sizeof(uint32_t), InvocationLogEntryType::CREATED_DIR);
    write(path_id);
    _entry_count++;
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
      const Entry &entry) throw(IoError) override {
    const uint32_t size =
        sizeof(Hash) +
        sizeof(uint32_t) +
        (sizeof(uint32_t) + sizeof(Fingerprint)) *
            (entry.input_files.size() + entry.output_files.size());
    const auto write_file_paths = [&](
        const std::vector<std::pair<std::string, Fingerprint>> &files) {
      for (const auto &file : files) {
        idForPath(file.first);
      }
    };
    write_file_paths(entry.input_files);
    write_file_paths(entry.output_files);

    writeHeader(size, InvocationLogEntryType::INVOCATION);

    write(build_step_hash);
    write(static_cast<uint32_t>(entry.output_files.size()));

    const auto write_files = [&](
        const std::vector<std::pair<std::string, Fingerprint>> &files) {
      for (const auto &file : files) {
        const auto it = _path_ids.find(file.first);
        assert(it != _path_ids.end());
        write(it->second);
        write(file.second);
      }
    };

    write_files(entry.output_files);
    write_files(entry.input_files);

    _entry_count++;
  }

  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override {
    writeHeader(sizeof(Hash), InvocationLogEntryType::DELETED);
    write(build_step_hash);
    _entry_count++;
  }

 private:
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

  void writePath(const std::string &path) {
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

  /**
   * Get the id for a path. If the path is not already written, write an entry
   * with that path. This means that this method cannot be called in the middle
   * of writing another entry.
   */
  uint32_t idForPath(const std::string &path) {
    const auto it = _path_ids.find(path);
    if (it == _path_ids.end()) {
      const auto id = _entry_count;
      writePath(path);
      _path_ids[path] = id;
      return id;
    } else {
      return it->second;
    }
  }

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

  // "Map" from entry id to path. Entries that aren't path entries are empty
  std::vector<Optional<Path>> paths_by_id;

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

      case InvocationLogEntryType::CREATED_DIR: {
        result.invocations.created_directories.insert(
            readPath(paths_by_id, entry));
        break;
      }

      case InvocationLogEntryType::INVOCATION: {
        const auto hash = read<Hash>(entry);
        entry = advance(entry, sizeof(hash));
        const auto outputs = read<uint32_t>(entry);
        entry = advance(entry, sizeof(outputs));
        const auto output_size =
            (sizeof(uint32_t) + sizeof(Fingerprint)) * outputs;
        if (entry._len < output_size) {
          throw ParseError("invalid invocation log: truncated invocation");
        }

        result.invocations.entries[hash] = {
            readFingerprints(paths_by_id, StringPiece(entry._str, output_size)),
            readFingerprints(paths_by_id, advance(entry, output_size)) };
        break;
      }

      case InvocationLogEntryType::DELETED: {
        if (entry._len == sizeof(uint32_t)) {
          // Deleted directory
          result.invocations.created_directories.erase(
              readPath(paths_by_id, entry));
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
    const std::string &log_path,
    InvocationLogParseResult::ParseData &&parse_data) throw(IoError) {
  return std::unique_ptr<InvocationLog>(
      new PersistentInvocationLog(
          file_system.open(log_path, "ab"),
          std::move(parse_data)));
}

void recompactPersistentInvocationLog(
    FileSystem &file_system,
    const Invocations &invocations,
    const std::string &log_path) throw(IoError) {
  const auto tmp_path = file_system.mkstemp("shk.tmp.log.XXXXXXXX");
  const auto log =
      openPersistentInvocationLog(
          file_system, tmp_path, InvocationLogParseResult::ParseData());

  for (const auto &dir : invocations.created_directories) {
    log->createdDirectory(dir.original());
  }

  for (const auto &entry : invocations.entries) {
    InvocationLog::Entry log_entry;
    const auto files = [&](
        const std::vector<std::pair<Path, Fingerprint>> &files) {
      std::vector<std::pair<std::string, Fingerprint>> result;
      for (const auto &file : files) {
        result.emplace_back(file.first.original(), file.second);
      }
      return result;
    };
    log_entry.output_files = files(entry.second.output_files);
    log_entry.input_files = files(entry.second.input_files);
    log->ranCommand(entry.first, log_entry);
  }

  file_system.rename(tmp_path, log_path);
}

}  // namespace shk
