#include "in_memory_invocation_log.h"

namespace shk {
namespace {

std::vector<std::pair<std::string, Fingerprint>> processInputPaths(
    FileSystem &fs,
    const Clock &clock,
    std::vector<std::string> &&dependencies) {
  std::vector<std::pair<std::string, Fingerprint>> files;
  for (auto &dep : dependencies) {
    const auto fingerprint = takeFingerprint(fs, clock(), dep);
    if (!fingerprint.stat.isDir()) {
      files.emplace_back(std::move(dep), fingerprint);
    }
  }
  return files;
}

std::vector<std::pair<std::string, Fingerprint>> processOutputPaths(
    FileSystem &fs,
    const Clock &clock,
    std::vector<std::string> &&paths) {
  std::vector<std::pair<std::string, Fingerprint>> files;
  for (auto &&path : paths) {
    files.emplace_back(std::move(path), takeFingerprint(fs, clock(), path));
  }
  return files;
}

}  // anonymous namespace

InMemoryInvocationLog::InMemoryInvocationLog(
    FileSystem &file_system, const Clock &clock)
    : _fs(file_system), _clock(clock) {}

void InMemoryInvocationLog::createdDirectory(const std::string &path) throw(IoError) {
  _created_directories.insert(path);
}

void InMemoryInvocationLog::removedDirectory(const std::string &path) throw(IoError) {
  _created_directories.erase(path);
}

Fingerprint InMemoryInvocationLog::fingerprint(const std::string &path) {
  return takeFingerprint(_fs, _clock(), path);
}

void InMemoryInvocationLog::ranCommand(
    const Hash &build_step_hash,
    std::vector<std::string> &&output_files,
    std::vector<std::string> &&input_files) throw(IoError) {
  auto output_file_fingerprints = processOutputPaths(_fs, _clock, std::move(output_files));

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
      processInputPaths(_fs, _clock, std::move(input_files)) };
}

void InMemoryInvocationLog::cleanedCommand(
    const Hash &build_step_hash) throw(IoError) {
  _entries.erase(build_step_hash);
}

Invocations InMemoryInvocationLog::invocations(Paths &paths) const {
  Invocations result;

  for (const auto &dir : _created_directories) {
    const auto path = paths.get(dir);
    path.fileId().each([&](const FileId file_id) {
      result.created_directories.emplace(file_id, path);
    });
  }

  const auto files = [&](
      const std::vector<std::pair<std::string, Fingerprint>> &files) {
    std::vector<size_t> out;
    for (const auto &file : files) {
      out.push_back(result.fingerprints.size());
      result.fingerprints.emplace_back(paths.get(file.first), file.second);
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
