#include "log/delayed_invocation_log.h"

#include <assert.h>
#include <limits>
#include <stdexcept>
#include <vector>

namespace shk {
namespace {

class DelayedInvocationLog : public InvocationLog {
 public:
  DelayedInvocationLog(
      const Clock &clock, std::unique_ptr<InvocationLog> &&inner_log)
      : _clock(clock),
        _inner_log(std::move(inner_log)) {
    assert(_inner_log);
  }

  ~DelayedInvocationLog() {
    // There is an off by one lurking here; if the time actually is LONG_MAX and
    // a command was written that second, this won't write all the entries and the
    // assert will trigger. For now, I'm going to ignore that.
    writeDelayedEntries(std::numeric_limits<time_t>::max());
    assert(_delayed_entries.empty());
  }

  void createdDirectory(const std::string &path) throw(IoError) override {
    // Directories are not fingerprinted and do not interact with the command
    // logging so this can be forwarded immediately.
    _inner_log->createdDirectory(path);
  }

  void removedDirectory(const std::string &path) throw(IoError) override {
    // Directories are not fingerprinted and do not interact with the command
    // logging so this can be forwarded immediately.
    _inner_log->removedDirectory(path);
  }

  std::pair<Fingerprint, FileId> fingerprint(const std::string &path) override {
    return _inner_log->fingerprint(path);
  }

  void ranCommand(
      const Hash &build_step_hash,
      std::vector<std::string> &&output_files,
      std::vector<Fingerprint> &&output_fingerprints,
      std::vector<std::string> &&input_files,
      std::vector<Fingerprint> &&input_fingerprints)
          throw(IoError) override {
    const auto now = _clock();
    writeDelayedEntries(now);

    DelayedEntry entry;
    entry.timestamp = now;
    entry.build_step_hash = build_step_hash;
    entry.output_files = std::move(output_files);
    entry.output_fingerprints = std::move(output_fingerprints);
    entry.input_files = std::move(input_files);
    entry.input_fingerprints = std::move(input_fingerprints);
    _delayed_entries.push_back(std::move(entry));
  }

  void cleanedCommand(
      const Hash &build_step_hash) throw(IoError) override {
    const auto now = _clock();
    writeDelayedEntries(now);

    DelayedEntry entry;
    entry.timestamp = now;
    entry.cleaned = true;
    entry.build_step_hash = build_step_hash;
    _delayed_entries.push_back(std::move(entry));
  }

 private:
  /**
   * Writes all the delayed entries that are strictly older than the timestamp
   * now.
   */
  void writeDelayedEntries(time_t now) {
    auto it = _delayed_entries.begin();
    for (; it != _delayed_entries.end(); ++it) {
      auto &delayed_entry = *it;
      if (delayed_entry.timestamp >= now) {
        break;
      }
      if (delayed_entry.cleaned) {
        _inner_log->cleanedCommand(delayed_entry.build_step_hash);
      } else {
        // Because Shuriken assumes that output files and input files of the
        // build are not modified by anything except build steps (which are
        // monitored for file modifications), and that Shuriken ensures that
        // only one build step modifies each output file and that there are no
        // steps that modify previous steps' inputs, it is safe to assume that
        // the fingerprint that was taken prior to invoking ranCommand (about
        // one second ago) has not been changed since. Because of that it is
        // safe to update the fingerprint timestamp to now, which in turn will
        // avoid racily clean fingerprints.
        setFingerprintTimestamps(delayed_entry.output_fingerprints, now);
        setFingerprintTimestamps(delayed_entry.input_fingerprints, now);

        _inner_log->ranCommand(
            delayed_entry.build_step_hash,
            std::move(delayed_entry.output_files),
            std::move(delayed_entry.output_fingerprints),
            std::move(delayed_entry.input_files),
            std::move(delayed_entry.input_fingerprints));
      }
    }
    _delayed_entries.erase(_delayed_entries.begin(), it);
  }

  static void setFingerprintTimestamps(
      std::vector<Fingerprint> &fingerprints,
      time_t now) {
    for (auto &fingerprint : fingerprints) {
      fingerprint.timestamp = now;
    }
  }

  struct DelayedEntry {
    time_t timestamp = 0;
    // true if this entry is for a cleanedCommand invocation
    bool cleaned = false;
    Hash build_step_hash;
    std::vector<std::string> output_files;
    std::vector<Fingerprint> output_fingerprints;
    std::vector<std::string> input_files;
    std::vector<Fingerprint> input_fingerprints;
  };

  const Clock _clock;
  const std::unique_ptr<InvocationLog> _inner_log;
  /**
   * Entries are always appended to the end of the vector. The class assumes
   * that timestamps of the entries are non-decreasing.
   */
  std::vector<DelayedEntry> _delayed_entries;
};

}  // anonymous namespace

std::unique_ptr<InvocationLog> delayedInvocationLog(
    const Clock &clock,
    std::unique_ptr<InvocationLog> &&inner_log) {
  return std::unique_ptr<InvocationLog>(
      new DelayedInvocationLog(clock, std::move(inner_log)));
}

}  // namespace shk
