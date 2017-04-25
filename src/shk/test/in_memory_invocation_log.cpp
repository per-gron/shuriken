#include "in_memory_invocation_log.h"

#include <stdlib.h>

namespace shk {
namespace {

std::vector<std::pair<std::string, Fingerprint>> processInputPaths(
    FileSystem &fs,
    const Clock &clock,
    std::vector<std::string> &&input_paths,
    std::vector<Fingerprint> &&input_fingerprints) {
  std::vector<std::pair<std::string, Fingerprint>> files;
  if (input_paths.size() != input_fingerprints.size()) {
    abort();
  }

  for (int i = 0; i < input_paths.size(); i++) {
    auto &path = input_paths[i];
    const auto &fingerprint = input_fingerprints[i];
    if (!fingerprint.stat.isDir()) {
      files.emplace_back(std::move(path), fingerprint);
    }
  }
  return files;
}

std::vector<std::pair<std::string, Fingerprint>> mergeOutputVectors(
    std::vector<std::string> &&paths,
    std::vector<Fingerprint> &&output_fingerprints) {
  std::vector<std::pair<std::string, Fingerprint>> files;
  if (paths.size() != output_fingerprints.size()) {
    abort();
  }
  for (int i = 0; i < paths.size(); i++) {
    files.emplace_back(std::move(paths[i]), std::move(output_fingerprints[i]));
  }
  return files;
}

struct InvocationsBuffer {
  std::vector<std::unique_ptr<const std::string>> strings;

  nt_string_view bufferString(nt_string_view str) {
    strings.emplace_back(new std::string(str));
    return *strings.back();
  }
};

}  // anonymous namespace

InMemoryInvocationLog::InMemoryInvocationLog(
    FileSystem &file_system, const Clock &clock)
    : _fs(file_system), _clock(clock) {}

void InMemoryInvocationLog::createdDirectory(
    nt_string_view path) throw(IoError) {
  _created_directories.insert(std::string(path));
}

void InMemoryInvocationLog::removedDirectory(
    nt_string_view path) throw(IoError) {
  _created_directories.erase(std::string(path));
}

std::pair<Fingerprint, FileId> InMemoryInvocationLog::fingerprint(
    const std::string &path) {
  return takeFingerprint(_fs, _clock(), path);
}

void InMemoryInvocationLog::ranCommand(
    const Hash &build_step_hash,
    std::vector<std::string> &&output_files,
    std::vector<Fingerprint> &&output_fingerprints,
    std::vector<std::string> &&input_files,
    std::vector<Fingerprint> &&input_fingerprints) throw(IoError) {
  auto output_file_fingerprints = mergeOutputVectors(
      std::move(output_files),
      std::move(output_fingerprints));

  auto files_end = std::partition(
      output_file_fingerprints.begin(),
      output_file_fingerprints.end(),
      [](const std::pair<std::string, Fingerprint> &x) {
        return !x.second.stat.isDir();
      });

  for (auto it = files_end; it != output_file_fingerprints.end(); ++it) {
    createdDirectory(it->first);
  }
  output_file_fingerprints.erase(files_end, output_file_fingerprints.end());

  _entries[build_step_hash] = {
      output_file_fingerprints,
      processInputPaths(
          _fs,
          _clock,
          std::move(input_files),
          std::move(input_fingerprints)) };
}

void InMemoryInvocationLog::cleanedCommand(
    const Hash &build_step_hash) throw(IoError) {
  _entries.erase(build_step_hash);
}

void InMemoryInvocationLog::leakMemory() {
  _has_leaked = true;
}

Invocations InMemoryInvocationLog::invocations() const {
  Invocations result;
  auto buffer = std::make_shared<InvocationsBuffer>();
  result.buffer = buffer;

  for (const auto &dir : _created_directories) {
    const auto stat = _fs.lstat(dir);
    if (stat.result == 0) {
      const auto file_id = FileId(stat);
      result.created_directories.emplace(file_id, buffer->bufferString(dir));
    }
  }

  // For deduplication
  std::unordered_map<std::string, std::unordered_map<Fingerprint, uint32_t>>
      fp_paths;

  const auto files = [&](
      const std::vector<std::pair<std::string, Fingerprint>> &files) {
    std::vector<uint32_t> out;
    for (const auto &file : files) {
      auto &fps = fp_paths[file.first];
      const auto fps_it = fps.find(file.second);
      if (fps_it == fps.end()) {
        fps[file.second] = result.fingerprints.size();
        result.fingerprints.emplace_back(
            buffer->bufferString(file.first),
            file.second);
      }
      out.push_back(fps[file.second]);
    }
    return out;
  };

  for (const auto &log_entry : _entries) {
    Invocations::Entry entry;
    entry.output_files = files(log_entry.second.output_files);
    entry.input_files = files(log_entry.second.input_files);
    result.entries[log_entry.first] = entry;
  }

  return result;
}

}  // namespace shk
