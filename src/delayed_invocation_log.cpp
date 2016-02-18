#include "delayed_invocation_log.h"

#include <assert.h>
#include <limits>
#include <stdexcept>

namespace shk {

DelayedInvocationLog::DelayedInvocationLog(
    const Clock &clock, std::unique_ptr<InvocationLog> &&inner_log)
    : _clock(clock),
      _inner_log(std::move(inner_log)) {
  assert(_inner_log);
}

DelayedInvocationLog::~DelayedInvocationLog() {
  if (!_write_all_called) {
    throw std::logic_error(
        "Destroyed DelayedInvocationLog without calling writeAll");
  }
}

void DelayedInvocationLog::createdDirectory(
    const std::string &path) throw(IoError) {
  // Directories are not fingerprinted and do not interact with the command
  // logging so this can be forwarded immediately.
  _inner_log->createdDirectory(path);
}

void DelayedInvocationLog::removedDirectory(
    const std::string &path) throw(IoError) {
  // Directories are not fingerprinted and do not interact with the command
  // logging so this can be forwarded immediately.
  _inner_log->removedDirectory(path);
}

void DelayedInvocationLog::ranCommand(
    const Hash &build_step_hash,
    std::unordered_set<std::string> &&output_files,
    std::unordered_map<std::string, DependencyType> &&input_files)
        throw(IoError) {
  _write_all_called = false;
  const auto now = _clock();
  writeDelayedEntries(now);

  DelayedEntry entry;
  entry.timestamp = now;
  entry.build_step_hash = build_step_hash;
  entry.output_files = std::move(output_files);
  entry.input_files = std::move(input_files);
  _delayed_entries.push_back(std::move(entry));
}

void DelayedInvocationLog::cleanedCommand(
    const Hash &build_step_hash) throw(IoError) {
  _write_all_called = false;
  const auto now = _clock();
  writeDelayedEntries(now);

  DelayedEntry entry;
  entry.timestamp = now;
  entry.cleaned = true;
  entry.build_step_hash = build_step_hash;
  _delayed_entries.push_back(std::move(entry));
}

void DelayedInvocationLog::writeAll() {
  // There is an off by one lurking here; if the time actually is LONG_MAX and
  // a command was written that second, this won't write all the entries and the
  // assert will trigger. For now, I'm going to ignore that.
  writeDelayedEntries(std::numeric_limits<time_t>::max());
  assert(_delayed_entries.empty());
  _write_all_called = true;
}

void DelayedInvocationLog::writeDelayedEntries(time_t now) {
  auto it = _delayed_entries.begin();
  for (; it != _delayed_entries.end(); ++it) {
    auto &delayed_entry = *it;
    if (delayed_entry.timestamp >= now) {
      break;
    }
    if (delayed_entry.cleaned) {
      _inner_log->cleanedCommand(delayed_entry.build_step_hash);
    } else {
      _inner_log->ranCommand(
          delayed_entry.build_step_hash,
          std::move(delayed_entry.output_files),
          std::move(delayed_entry.input_files));
    }
  }
  _delayed_entries.erase(_delayed_entries.begin(), it);
}


}  // namespace shk
