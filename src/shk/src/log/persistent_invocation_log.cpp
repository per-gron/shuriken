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

#include "log/persistent_invocation_log.h"

#include <assert.h>
#include <errno.h>

#include "optional.h"
#include "string_view.h"

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

string_view advance(string_view view, size_t len) {
  assert(len <= view.size());
  return string_view(view.data() + len, view.size() - len);
}

bool parseInvocationLogSignature(string_view *view, std::string *err) {
  const auto signature_size = kFileSignature.size() + sizeof(kFileVersion);
  if (view->size() < signature_size) {
    *err = "invalid invocation log file signature (too short)";
    return false;
  }

  if (!std::equal(kFileSignature.begin(), kFileSignature.end(), view->data())) {
    *err = "invalid invocation log file signature";
    return false;
  }

  const auto version =
      *reinterpret_cast<const uint32_t *>(view->data() + kFileSignature.size());
  if (version != kFileVersion) {
    *err = "invalid invocation log file version or bad byte order";
    return false;
  }

  *view = advance(*view, signature_size);

  return true;
}

class EntryHeader {
 public:
  using Value = uint32_t;

  EntryHeader(string_view view) throw(ParseError) {
    if (view.size() < sizeof(Value)) {
      throw ParseError("invalid invocation log: encountered truncated entry");
    }

    _header = *reinterpret_cast<const uint32_t *>(view.data());
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
    string_view view, size_t min_size) throw(ParseError) {
  if (view.size() < min_size) {
    throw ParseError("invalid invocation log: encountered invalid entry");
  }
}

template<typename T>
const T &read(string_view view) {
  ensureEntryLen(view, sizeof(T));
  return *reinterpret_cast<const T *>(view.data());
}

template<typename T>
T readEntryById(
    const std::vector<Optional<T>> &entries_by_id,
    string_view view) throw(ParseError) {
  const auto entry_id = read<uint32_t>(view);
  if (entry_id >= entries_by_id.size() || !entries_by_id[entry_id]) {
    throw ParseError(
        "invalid invocation log: encountered invalid fingerprint ref");
  }
  return *entries_by_id[entry_id];
}

std::vector<size_t> readFingerprints(
    const std::vector<Optional<size_t>> &fingerprints_by_id,
    string_view view) {
  std::vector<size_t> result;
  result.reserve(view.size() / sizeof(uint32_t));

  for (; view.size(); view = advance(view, sizeof(uint32_t))) {
    result.push_back(readEntryById(fingerprints_by_id, view));
  }

  return result;
}

const std::string &readPath(
    const std::vector<Optional<std::string>> &paths_by_id,
    string_view view) throw(ParseError) {
  const auto path_id = read<uint32_t>(view);
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

  std::pair<Fingerprint, FileId> fingerprint(const std::string &path) override {
    const auto it = _fingerprint_ids.find(path);
    if (it == _fingerprint_ids.end()) {
      // No prior entry for that path. Need to take fingerprint.
      return takeFingerprint(_fs, _clock(), path);
    } else {
      // There is a fingerprint entry for the given path already.
      const auto &old_fingerprint = it->second.fingerprint;
      return retakeFingerprint(_fs, _clock(), path, old_fingerprint);
    }
  }

  void ranCommand(
      const Hash &build_step_hash,
      std::vector<std::string> &&output_files,
      std::vector<Fingerprint> &&output_fingerprints,
      std::vector<std::string> &&input_files_map,
      std::vector<Fingerprint> &&input_fingerprints)
          throw(IoError) override {

    output_files = writeOutputPathsAndFingerprints(
        std::move(output_files),
        std::move(output_fingerprints));
    const auto input_files = writeInputPathsAndFingerprints(
        std::move(input_files_map),
        std::move(input_fingerprints));

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

  /**
   * Helper function that is useful when recompacting. This method does not
   * re-take fingerprints so it is not suitable for re-logging a racily clean
   * entry.
   *
   * The fingerprints parameter has the same format and purpose as
   * Invocations::fingerprints. The output_files and input_files vectors contain
   * indices into this array.
   */
  void relogCommand(
      const Hash &build_step_hash,
      const std::vector<std::pair<std::string, Fingerprint>> &fingerprints,
      const std::vector<size_t> &output_files,
      const std::vector<size_t> &input_files) {
    std::vector<std::string> output_paths;
    std::vector<Fingerprint> output_fingerprints;
    for (const auto &file_idx : output_files) {
      const auto &file = fingerprints[file_idx];
      output_paths.push_back(file.first);
      output_fingerprints.push_back(file.second);
    }

    std::vector<std::string> input_paths;
    std::vector<Fingerprint> input_fingerprints;
    for (const auto &file_idx : input_files) {
      const auto &file = fingerprints[file_idx];
      input_paths.push_back(file.first);
      input_fingerprints.push_back(file.second);
    }

    ranCommand(
        build_step_hash,
        std::move(output_paths),
        std::move(output_fingerprints),
        std::move(input_paths),
        std::move(input_fingerprints));
  }

 private:
  void writeFiles(const std::vector<std::string> &paths) {
    for (const auto &path : paths) {
      const auto fingerprint_it = _fingerprint_ids.find(path);
      assert(fingerprint_it != _fingerprint_ids.end());
      write(fingerprint_it->second.record_id);
    }
  }

  /**
   * Used for output files.
   */
  std::vector<std::string> writeOutputPathsAndFingerprints(
      std::vector<std::string> &&paths,
      std::vector<Fingerprint> &&output_fingerprints) {
    if (paths.size() != output_fingerprints.size()) {
      // Should never happen
      throw std::runtime_error("mismatching path and fingerprint vector sizes");
    }
    std::vector<std::string> result;
    for (int i = 0; i < paths.size(); i++) {
      const auto &path = paths[i];
      const auto &fingerprint = output_fingerprints[i];
      ensureRecentFingerprintIsWritten(
          path, fingerprint, WriteType::DIRECTORY_AS_DIRECTORY_ENTRY);
      if (!fingerprint.stat.isDir()) {
        result.push_back(std::move(path));
      }
    }
    return result;
  }

  std::vector<std::string> writeInputPathsAndFingerprints(
      std::vector<std::string> &&input_files,
      std::vector<Fingerprint> &&input_fingerprints) {
    if (input_files.size() != input_fingerprints.size()) {
      // Should never happen
      throw std::runtime_error("mismatching path and fingerprint vector sizes");
    }
    std::vector<std::string> result;
    for (int i = 0; i < input_files.size(); i++) {
      auto &path = input_files[i];
      const auto &fingerprint = input_fingerprints[i];
      ensureRecentFingerprintIsWritten(
          path, fingerprint, WriteType::ALWAYS_FINGERPRINT);
      if (!fingerprint.stat.isDir()) {
        result.push_back(std::move(path));
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
        reinterpret_cast<const uint8_t *>(&fingerprint),
        sizeof(fingerprint),
        1);

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
   * Given a fingerprint, ensure that it is written in the log. If there already
   * is one, this method does not modify the log.
   *
   * Because this might write an entry to the log, this method cannot be called
   * in the middle of writing another entry.
   */
  void ensureRecentFingerprintIsWritten(
        const std::string &path,
        const Fingerprint &fingerprint,
        WriteType type) {
    const auto path_id = ensurePathIsWritten(path);

    FingerprintIdsValue value;
    value.fingerprint = fingerprint;
    value.record_id = _entry_count;
    const auto it = _fingerprint_ids.find(path);
    if (it == _fingerprint_ids.end()) {
      // No prior entry for that path.
      writeFingerprintOrDirectoryEntry(path_id, value.fingerprint, type);
      _fingerprint_ids[path] = value;
    } else {
      // There is a fingerprint entry for the given path already. Find out if it
      // can be reused or if a new fingerprint is required.
      const auto &old_fingerprint = it->second.fingerprint;
      if (old_fingerprint != value.fingerprint) {
        writeFingerprintOrDirectoryEntry(path_id, value.fingerprint, type);
        _fingerprint_ids[path] = value;
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
  auto view = mmap->memory();
  const auto file_size = view.size();

  std::string err;
  if (!parseInvocationLogSignature(&view, &err)) {
    // Parsing of log signature failed. Remove the file so that the error goes
    // away.
    file_system.unlink(log_path);

    result.warning = err;
    return result;
  }

  // "Map" from path entry id to path. Entries that aren't path entries are
  // empty.
  std::vector<Optional<std::string>> paths_by_id;
  // "Map" from fingerprint entry id to index in Invocations::fingerprints.
  // Indices that refer to entries that have been read but arent fingerprint
  // entries are empty.
  std::vector<Optional<size_t>> fingerprints_by_id;

  auto &entry_count = result.parse_data.entry_count;

  try {
    for (; view.size(); entry_count++) {
      EntryHeader header(view);
      const auto entry_size = header.entrySize();
      ensureEntryLen(view, entry_size + sizeof(EntryHeader::Value));
      string_view entry(view.data() + sizeof(EntryHeader::Value), entry_size);

      switch (header.entryType()) {
      case InvocationLogEntryType::PATH: {
        paths_by_id.resize(entry_count + 1);
        if (strnlen(entry.data(), entry.size()) == entry.size()) {
          throw ParseError(
              "invalid invocation log: Encountered non null terminated path");
        }
        // Don't use entry.asString() because it could make the string contain
        // trailing \0s
        auto path_string = std::string(entry.data());
        result.parse_data.path_ids[path_string] = entry_count;
        paths_by_id[entry_count] = std::move(path_string);
        break;
      }

      case InvocationLogEntryType::CREATED_DIR_OR_FINGERPRINT: {
        const bool is_created_dir = entry_size == sizeof(uint32_t);
        if (is_created_dir) {
          const auto path = readPath(paths_by_id, entry);
          const auto stat = file_system.lstat(path);
          if (stat.result == 0) {
            // Only add the directory to the resulting Invocations object if the
            // file exists. For more info see Invocations::created_directories
            const auto file_id = FileId(stat);
            result.invocations.created_directories.emplace(
                file_id, path);
          }
        } else if (entry_size == sizeof(uint32_t) + sizeof(Fingerprint)) {
          const auto path = readPath(paths_by_id, entry);
          entry = advance(entry, sizeof(uint32_t));

          FingerprintIdsValue value;
          value.record_id = entry_count;
          value.fingerprint =
              *reinterpret_cast<const Fingerprint *>(entry.data());
          result.parse_data.fingerprint_ids[path] = value;

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
        if (entry.size() < output_size) {
          throw ParseError("invalid invocation log: truncated invocation");
        }

        result.invocations.entries[hash] = {
            readFingerprints(
                fingerprints_by_id,
                string_view(entry.data(), output_size)),
            readFingerprints(
                fingerprints_by_id,
                advance(entry, output_size)) };
        break;
      }

      case InvocationLogEntryType::DELETED: {
        if (entry.size() == sizeof(uint32_t)) {
          // Deleted directory
          const auto path = readPath(paths_by_id, entry);
          const auto stat = file_system.lstat(path);
          if (stat.result == 0) {
            const auto file_id = FileId(stat);
            result.invocations.created_directories.erase(file_id);
          }
        } else if (entry.size() == sizeof(Hash)) {
          // Deleted invocation
          result.invocations.entries.erase(read<Hash>(entry));
        } else {
          throw ParseError("invalid invocation log: invalid deleted entry");
        }
        break;
      }
      }

      // Now that we are sure that the parsing succeeded, advance view. This
      // is important because the truncation logic below depends on view
      // pointing to the end of a valid entry.
      view = advance(view, sizeof(EntryHeader::Value) + entry_size);
    }
  } catch (ParseError &error) {
    // Parse error while parsing the invocation log. Treat this as a warning and
    // truncate the invocation log to the last known valid entry.
    result.warning = error.what();
  }

  if (view.size() != 0) {
    // Parsing failed. Truncate the file to a known valid state
    file_system.truncate(log_path, file_size - view.size());
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
  PersistentInvocationLog log(
      file_system,
      clock,
      file_system.open(tmp_path, "ab"),
      InvocationLogParseResult::ParseData());

  for (const auto &dir : invocations.created_directories) {
    log.createdDirectory(dir.second);
  }

  for (const auto &entry : invocations.entries) {
    log.relogCommand(
        entry.first,
        invocations.fingerprints,
        entry.second.output_files,
        entry.second.input_files);
  }

  file_system.rename(tmp_path, log_path);
}

}  // namespace shk
