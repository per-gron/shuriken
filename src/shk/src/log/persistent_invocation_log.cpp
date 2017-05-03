// Copyright 2012 Google Inc. All Rights Reserved.
// Copyright 2017 Per Grön. All Rights Reserved.
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

/**
 * The PersistentInvocationLog object has some maps with non-owning string_views
 * in them (PathIds, FingerprintIds). After parsing the invocation log, all of
 * those string_views point to memory inside the invocation log itself, to save
 * copying.
 *
 * However, when the log is written to, for example with the ranCommand method,
 * the PathIds and FingerprintIds objects have to be updated, but they can't own
 * the data that they have. In order to ensure that the data survives, this
 * struct owns the data that would otherwise not been owned by anyone.
 *
 * No, this is not particularly elegant :-( but on the other hand, parsing the
 * invocation log is highly performance sensitive code and not copying things
 * really helps.
 */
struct InvocationsBuffer {
  InvocationsBuffer() = default;
  explicit InvocationsBuffer(std::shared_ptr<void> inner_buffer)
      : inner_buffer(inner_buffer) {}

  std::shared_ptr<void> inner_buffer;
  std::vector<std::unique_ptr<const std::string>> strings;
  std::vector<std::unique_ptr<Fingerprint>> fingerprints;

  nt_string_view bufferString(nt_string_view str) {
    strings.emplace_back(new std::string(str));
    return *strings.back();
  }

  const Fingerprint &bufferFingerprint(const Fingerprint &fp) {
    fingerprints.emplace_back(new Fingerprint(fp));
    return *fingerprints.back();
  }
};

enum class InvocationLogEntryType : uint32_t {
  PATH = 0,
  CREATED_DIR_OR_FINGERPRINT = 1,
  INVOCATION = 2,
  DELETED = 3,
};

const std::string kFileSignature = "invocations:";
constexpr uint32_t kFileVersion = 1;
constexpr uint32_t kInvocationLogEntryTypeMask = 3;
constexpr uint32_t kInvalidEntry = std::numeric_limits<uint32_t>::max();

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

IndicesView readFingerprints(
    uint32_t fingerprint_count,
    string_view view) {
  const uint32_t *begin = reinterpret_cast<const uint32_t *>(view.data());
  const uint32_t *end = begin + view.size() / sizeof(uint32_t);
  IndicesView fingerprint_ids(begin, end);

  for (const auto fingerprint_id : fingerprint_ids) {
    if (fingerprint_id >= fingerprint_count) {
      throw ParseError(
          "invalid invocation log: encountered invalid fingerprint ref");
    }
  }

  return fingerprint_ids;
}

nt_string_view readPath(
    const std::vector<nt_string_view> &paths_by_id,
    string_view view) throw(ParseError) {
  const auto path_id = read<uint32_t>(view);
  if (path_id >= paths_by_id.size()) {
    throw ParseError(
        "invalid invocation log: encountered invalid path ref");
  }
  return paths_by_id[path_id];
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
        _fingerprint_entry_count(parse_data.fingerprint_entry_count),
        _path_entry_count(parse_data.path_entry_count),
        _buffer(std::make_shared<InvocationsBuffer>(parse_data.buffer)) {
    writeHeader();
  }

  void createdDirectory(nt_string_view path) throw(IoError) override {
    const auto path_id = ensurePathIsWritten(path).first;
    writeDirectoryEntry(path_id);
  }

  void removedDirectory(nt_string_view path) throw(IoError) override {
    const auto it = _path_ids.find(path);
    if (it == _path_ids.end()) {
      // The directory has not been created so it can't be removed.
      return;
    }
    writeHeader(sizeof(uint32_t), InvocationLogEntryType::DELETED);
    write(it->second);
  }

  std::pair<Fingerprint, FileId> fingerprint(const std::string &path) override {
    const auto it = _fingerprint_ids.find(path);
    if (it == _fingerprint_ids.end()) {
      // No prior entry for that path. Need to take fingerprint.
      return takeFingerprint(_fs, _clock(), path);
    } else {
      // There is a fingerprint entry for the given path already.
      const auto &old_fingerprint = *it->second.fingerprint;
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

    const auto output_fp_ids = writeOutputPathsAndFingerprints(
        std::move(output_files),
        std::move(output_fingerprints));
    const auto input_fp_ids = writeInputPathsAndFingerprints(
        std::move(input_files_map),
        std::move(input_fingerprints));

    const auto total_entries = output_fp_ids.size() + input_fp_ids.size();
    const uint32_t size =
        sizeof(Hash) +
        sizeof(uint32_t) +
        sizeof(uint32_t) * total_entries;

    writeHeader(size, InvocationLogEntryType::INVOCATION);

    write(build_step_hash);
    write(static_cast<uint32_t>(output_fp_ids.size()));

    writeFingerprintIds(output_fp_ids);
    writeFingerprintIds(input_fp_ids);
  }

  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override {
    writeHeader(sizeof(Hash), InvocationLogEntryType::DELETED);
    write(build_step_hash);
  }

  void leakMemory() override {
    new PathIds(std::move(_path_ids));
    new FingerprintIds(std::move(_fingerprint_ids));
    new InvocationsBuffer(std::move(*_buffer));
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
      const std::vector<std::pair<nt_string_view, const Fingerprint &>> &
          fingerprints,
      IndicesView output_files,
      IndicesView input_files) {
    std::vector<std::string> output_paths;
    std::vector<Fingerprint> output_fingerprints;
    for (const auto file_idx : output_files) {
      const auto &file = fingerprints[file_idx];
      output_paths.emplace_back(file.first);
      output_fingerprints.push_back(file.second);
    }

    std::vector<std::string> input_paths;
    std::vector<Fingerprint> input_fingerprints;
    for (const auto file_idx : input_files) {
      const auto &file = fingerprints[file_idx];
      input_paths.emplace_back(file.first);
      input_fingerprints.push_back(file.second);
    }

    ranCommand(
        build_step_hash,
        std::move(output_paths),
        std::move(output_fingerprints),
        std::move(input_paths),
        std::move(input_fingerprints));
  }

  /**
   * Extract a ParseData object that can be used when creating a future
   * PersistentInvocationLog instance. This steals information from the
   * object. After calling this method it is not legal to modify the log
   * through this object anymore.
   */
  InvocationLogParseResult::ParseData extractParseData() {
    InvocationLogParseResult::ParseData ans;
    ans.path_ids = std::move(_path_ids);
    ans.fingerprint_ids = std::move(_fingerprint_ids);
    ans.fingerprint_entry_count = _fingerprint_entry_count;
    ans.path_entry_count = _path_entry_count;
    ans.buffer = _buffer;
    return ans;
  }

 private:
  void writeFingerprintIds(const std::vector<uint32_t> &ids) {
    for (const auto &id : ids) {
      write(id);
    }
  }

  /**
   * Used for output files.
   */
  std::vector<uint32_t> writeOutputPathsAndFingerprints(
      std::vector<std::string> &&paths,
      std::vector<Fingerprint> &&output_fingerprints) {
    if (paths.size() != output_fingerprints.size()) {
      // Should never happen
      throw std::runtime_error("mismatching path and fingerprint vector sizes");
    }
    std::vector<uint32_t> result;
    for (int i = 0; i < paths.size(); i++) {
      const auto &path = paths[i];
      const auto &fingerprint = output_fingerprints[i];
      const auto entry_id = ensureRecentFingerprintIsWritten(
          path, fingerprint, WriteType::DIRECTORY_AS_DIRECTORY_ENTRY);
      if (entry_id != kInvalidEntry) {
        result.push_back(entry_id);
      }
    }
    return result;
  }

  std::vector<uint32_t> writeInputPathsAndFingerprints(
      std::vector<std::string> &&input_files,
      std::vector<Fingerprint> &&input_fingerprints) {
    if (input_files.size() != input_fingerprints.size()) {
      // Should never happen
      throw std::runtime_error("mismatching path and fingerprint vector sizes");
    }
    std::vector<uint32_t> result;
    for (int i = 0; i < input_files.size(); i++) {
      auto &path = input_files[i];
      const auto &fingerprint = input_fingerprints[i];
      const auto entry_id = ensureRecentFingerprintIsWritten(
          path, fingerprint, WriteType::IGNORE_DIRECTORY);
      if (entry_id != kInvalidEntry) {
        result.push_back(entry_id);
      }
    }
    return result;
  }

  void writeHeader() {
    IoError error;
    long tell;
    std::tie(tell, error) = _stream->tell();
    if (error) {
      throw error;
    }
    if (tell == 0) {
      if (auto error = _stream->write(
              reinterpret_cast<const uint8_t *>(kFileSignature.data()),
              kFileSignature.size(),
              1)) {
        throw error;
      }
      // The file version implicitly serves as a byte order mark
      write(kFileVersion);
    }
  }

  template<typename T>
  void write(const T &val) {
    if (auto error = _stream->write(
            reinterpret_cast<const uint8_t *>(&val), sizeof(val), 1)) {
      throw error;
    }
  }

  void writeHeader(uint32_t size, InvocationLogEntryType type) {
    assert((size & kInvocationLogEntryTypeMask) == 0);
    write(size | static_cast<uint32_t>(type));
  }

  /**
   * Write a Path entry to the invocation log.
   *
   * Takes a string_view into non-owned memory and returns a string_view into
   * memory that will survive as long as this object.
   */
  nt_string_view writePathEntry(nt_string_view path) {
    const auto path_size = path.size() + 1;
    const auto padding_bytes =
        (4 - (path_size & kInvocationLogEntryTypeMask)) % 4;
    writeHeader(
        path_size + padding_bytes,
        InvocationLogEntryType::PATH);

    if (auto error = _stream->write(
            reinterpret_cast<const uint8_t *>(path.data()), path_size, 1)) {
      throw error;
    }

    // Ensure that the output is 4-byte aligned.
    static const char *kNullBuf = "\0\0\0";
    if (auto error = _stream->write(
            reinterpret_cast<const uint8_t *>(kNullBuf), padding_bytes, 1)) {
      throw error;
    }

    _path_entry_count++;

    return _buffer->bufferString(path);
  }

  void writeDirectoryEntry(const uint32_t path_id) {
    writeHeader(
        sizeof(uint32_t), InvocationLogEntryType::CREATED_DIR_OR_FINGERPRINT);
    write(path_id);
  }

  /**
   * Write a fingerprint entry to the log.
   *
   * Returns the id for the written fingerprint.
   */
  uint32_t writeFingerprintEntry(
        const uint32_t path_id, const Fingerprint &fingerprint) {
    writeHeader(
        sizeof(path_id) + sizeof(fingerprint),
        InvocationLogEntryType::CREATED_DIR_OR_FINGERPRINT);

    write(path_id);
    if (auto error = _stream->write(
            reinterpret_cast<const uint8_t *>(&fingerprint),
            sizeof(fingerprint),
            1)) {
      throw error;
    }

    return _fingerprint_entry_count++;
  }

  /**
   * Get the id for a path. If the path is not already written, write an entry
   * with that path. This means that this method cannot be called in the middle
   * of writing another entry.
   *
   * Returns the id of the path, along with a string_view to the path that will
   * survive as long as this object (the path parameter is only known to survive
   * the scope).
   */
  std::pair<uint32_t, nt_string_view> ensurePathIsWritten(nt_string_view path) {
    const auto it = _path_ids.find(path);
    if (it == _path_ids.end()) {
      const auto id = _path_entry_count;
      auto owned_path = writePathEntry(path);
      _path_ids[owned_path] = id;
      return std::make_pair(id, owned_path);
    } else {
      return std::make_pair(it->second, it->first);
    }
  }

  enum class WriteType {
    IGNORE_DIRECTORY,
    DIRECTORY_AS_DIRECTORY_ENTRY
  };

  uint32_t writeFingerprintOrDirectoryEntry(
        uint32_t path_id,
        nt_string_view owned_path,
        const Fingerprint &fingerprint,
        WriteType type) {
    if (fingerprint.stat.isDir()) {
      if (type == WriteType::DIRECTORY_AS_DIRECTORY_ENTRY) {
        writeDirectoryEntry(path_id);
      }
      return kInvalidEntry;
    } else {
      FingerprintIdsValue value;
      value.record_id = _fingerprint_entry_count;
      value.fingerprint = &_buffer->bufferFingerprint(fingerprint);

      const auto entry_id = writeFingerprintEntry(path_id, fingerprint);
      _fingerprint_ids[owned_path] = value;

      return entry_id;
    }
  }

  /**
   * Given a fingerprint, ensure that it is written in the log. If there already
   * is one, this method does not modify the log.
   *
   * Because this might write an entry to the log, this method cannot be called
   * in the middle of writing another entry.
   *
   * Returns the fingerprint id for that fingerprint, or kInvalidEntry if no
   * fingerprint was written.
   */
  uint32_t ensureRecentFingerprintIsWritten(
        const std::string &path,
        const Fingerprint &fingerprint,
        WriteType type) {
    uint32_t path_id;
    nt_string_view owned_path;
    std::tie(path_id, owned_path) = ensurePathIsWritten(path);

    const auto it = _fingerprint_ids.find(owned_path);
    if (it == _fingerprint_ids.end()) {
      // No prior entry for that path.
      return writeFingerprintOrDirectoryEntry(
          path_id, owned_path, fingerprint, type);
    } else {
      // There is a fingerprint entry for the given path already. Find out if it
      // can be reused or if a new fingerprint is required.
      const auto &old_fingerprint = *it->second.fingerprint;
      if (old_fingerprint != fingerprint) {
        return writeFingerprintOrDirectoryEntry(
            path_id, owned_path, fingerprint, type);
      } else {
        return it->second.record_id;
      }
    }
  }

  FileSystem &_fs;
  const Clock _clock;
  const std::unique_ptr<FileSystem::Stream> _stream;
  PathIds _path_ids;
  FingerprintIds _fingerprint_ids;
  uint32_t _fingerprint_entry_count;
  uint32_t _path_entry_count;
  std::shared_ptr<InvocationsBuffer> _buffer;
};

void parsePath(
    string_view entry,
    InvocationLogParseResult &result,
    std::vector<nt_string_view> &paths_by_id) throw(ParseError) {
  if (strnlen(entry.data(), entry.size()) == entry.size()) {
    throw ParseError(
        "invalid invocation log: Encountered non null terminated path");
  }
  // The string contains an unknown number of trailing \0s, so construct the
  // string view in a way that ensures that those are not included.
  auto path_string_view = nt_string_view(entry.data());
  result.parse_data.path_ids[path_string_view] = paths_by_id.size();
  paths_by_id.emplace_back(path_string_view);
}

void parseCreatedDir(
    string_view entry,
    FileSystem &file_system,
    InvocationLogParseResult &result,
    std::vector<nt_string_view> &paths_by_id)
        throw(IoError, ParseError) {
  const auto path = readPath(paths_by_id, entry);
  const auto stat = file_system.lstat(path);
  if (stat.result == 0) {
    // Only add the directory to the resulting Invocations object if the
    // file exists. For more info see Invocations::created_directories
    const auto file_id = FileId(stat);
    result.invocations.created_directories.emplace(
        file_id, path);
  }
}

void parseFingerprint(
    string_view entry,
    InvocationLogParseResult &result,
    std::vector<nt_string_view> &paths_by_id) throw(ParseError) {
  const auto path = readPath(paths_by_id, entry);
  entry = advance(entry, sizeof(uint32_t));

  FingerprintIdsValue value;
  value.record_id = result.invocations.fingerprints.size();
  value.fingerprint =
      reinterpret_cast<const Fingerprint *>(entry.data());
  result.parse_data.fingerprint_ids[path] = value;

  result.invocations.fingerprints.emplace_back(path, *value.fingerprint);
}

void parseInvocation(
    string_view entry,
    InvocationLogParseResult &result) throw(ParseError) {
  const auto hash = read<Hash>(entry);
  entry = advance(entry, sizeof(hash));
  const auto outputs = read<uint32_t>(entry);
  entry = advance(entry, sizeof(outputs));
  const auto output_size = sizeof(uint32_t) * outputs;
  if (entry.size() < output_size) {
    throw ParseError("invalid invocation log: truncated invocation");
  }

  uint32_t fingerprint_count = result.invocations.fingerprints.size();
  result.invocations.entries[hash] = {
      readFingerprints(
          fingerprint_count,
          string_view(entry.data(), output_size)),
      readFingerprints(
          fingerprint_count,
          advance(entry, output_size)) };
}

void parseDeleted(
    string_view entry,
    FileSystem &file_system,
    InvocationLogParseResult &result,
    std::vector<nt_string_view> &paths_by_id) throw(ParseError) {
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
}

}  // anonymous namespace

InvocationLogParseResult parsePersistentInvocationLog(
    FileSystem &file_system,
    const std::string &log_path) throw(IoError, ParseError) {
  InvocationLogParseResult result;

#if 0
  // Use readFile instead of mmap for parsing the invocation log. This can
  // provide interesting insight when profiling because then other functions
  // don't look slow because of i/o bound page faults.
  //
  // According to my measurements, this is a little bit slower than mmap, so
  // it's not used normally.
  auto log_contents = std::make_shared<std::string>();
  try {
    *log_contents = file_system.readFile(log_path);
  } catch (IoError &io_error) {
    if (io_error.code() == ENOENT) {
      return result;
    } else {
      throw;
    }
  }
  auto view = string_view(*log_contents);
  result.invocations.buffer = log_contents;
#else
  std::shared_ptr<FileSystem::Mmap> mmap;
  IoError io_error;
  std::tie(mmap, io_error) = file_system.mmap(log_path);
  if (io_error) {
    if (io_error.code() == ENOENT) {
      return result;
    } else {
      throw io_error;
    }
  }
  auto view = mmap->memory();
  result.invocations.buffer = mmap;
#endif
  const auto file_size = view.size();

  std::string err;
  if (!parseInvocationLogSignature(&view, &err)) {
    // Parsing of log signature failed. Remove the file so that the error goes
    // away.
    if (auto error = file_system.unlink(log_path)) {
      throw error;
    }

    result.warning = err;
    return result;
  }

  // "Map" from path path entry id to path.
  std::vector<nt_string_view> paths_by_id;

  size_t entry_count = 0;

  try {
    for (; view.size(); entry_count++) {
      EntryHeader header(view);
      const auto entry_size = header.entrySize();
      ensureEntryLen(view, entry_size + sizeof(EntryHeader::Value));
      string_view entry(view.data() + sizeof(EntryHeader::Value), entry_size);

      switch (header.entryType()) {
      case InvocationLogEntryType::PATH: {
        parsePath(entry, result, paths_by_id);
        break;
      }

      case InvocationLogEntryType::CREATED_DIR_OR_FINGERPRINT: {
        const bool is_created_dir = entry_size == sizeof(uint32_t);
        if (is_created_dir) {
          parseCreatedDir(entry, file_system, result, paths_by_id);
        } else if (entry_size == sizeof(uint32_t) + sizeof(Fingerprint)) {
          parseFingerprint(entry, result, paths_by_id);
        } else {
          throw ParseError("invalid invocation log: truncated invocation");
        }
        break;
      }

      case InvocationLogEntryType::INVOCATION: {
        parseInvocation(entry, result);
        break;
      }

      case InvocationLogEntryType::DELETED: {
        parseDeleted(entry, file_system, result, paths_by_id);
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
    if (auto err = file_system.truncate(log_path, file_size - view.size())) {
      throw err;
    }
  }

  result.parse_data.fingerprint_entry_count =
      result.invocations.fingerprints.size();
  result.parse_data.path_entry_count = paths_by_id.size();

  // Rebuild the log if there are too many dead records.
  static constexpr int kMinCompactionEntryCount = 1000;
  static constexpr int kCompactionRatio = 3;
  const auto unique_record_count =
      result.invocations.entries.size() +
      result.invocations.created_directories.size() +
      result.parse_data.path_ids.size() +
      result.invocations.countUsedFingerprints();

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
  std::unique_ptr<FileSystem::Stream> stream;
  IoError error;
  std::tie(stream, error) = file_system.open(log_path, "ab");
  if (error) {
    throw error;
  }
  return std::unique_ptr<InvocationLog>(
      new PersistentInvocationLog(
          file_system, clock, std::move(stream), std::move(parse_data)));
}

InvocationLogParseResult::ParseData recompactPersistentInvocationLog(
    FileSystem &file_system,
    const Clock &clock,
    const Invocations &invocations,
    const std::string &log_path) throw(IoError) {
  std::string tmp_path;
  IoError error;
  std::tie(tmp_path, error) = file_system.mkstemp("shk.tmp.log.XXXXXXXX");
  if (error) {
    throw error;
  }

  std::unique_ptr<FileSystem::Stream> stream;
  std::tie(stream, error) = file_system.open(tmp_path, "ab");
  if (error) {
    throw error;
  }
  PersistentInvocationLog log(
      file_system,
      clock,
      std::move(stream),
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

  if (auto err = file_system.rename(tmp_path, log_path)) {
    throw err;
  }

  return log.extractParseData();
}

}  // namespace shk
