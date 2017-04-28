#include "debug_capture_log.h"

#include <errno.h>

#include <shk-trace/debug_capture_log_generated.h>

namespace shk {

using namespace ShkTraceDebugCaptureLog;

DebugCaptureLog::DebugCaptureLog(FileDescriptor &&fd)
    : _fd(std::move(fd)) {}

void DebugCaptureLog::writeTraceRequest(
    const TracingServer::TraceRequest &trace_request) {
  flatbuffers::FlatBufferBuilder builder;

  auto cwd = builder.CreateString(trace_request.cwd);

  auto tr = CreateTraceRequest(
      builder, trace_request.pid_to_trace, trace_request.root_thread_id, cwd);

  builder.Finish(CreateEntry(builder, tr, 0));

  writeToFile(builder.GetBufferPointer(), builder.GetSize());
}

void DebugCaptureLog::writeKdBufs(const kd_buf *begin, const kd_buf *end) {
  flatbuffers::FlatBufferBuilder builder;

  auto bufs_vector = builder.CreateVector(
      reinterpret_cast<const uint8_t *>(begin),
      (end - begin) * sizeof(kd_buf));

  auto kd_bufs = CreateKdBufs(builder, bufs_vector);

  builder.Finish(CreateEntry(builder, 0, kd_bufs));

  writeToFile(builder.GetBufferPointer(), builder.GetSize());
}

bool DebugCaptureLog::parse(
    const FileDescriptor &fd,
    const std::function<
        void (std::unique_ptr<TracingServer::TraceRequest> &&)> &
            trace_request_callback,
    const std::function<void (
        const kd_buf *begin, const kd_buf *end)> &kd_bufs_callback,
    std::string *err) {
  std::vector<uint8_t> file;
  char buf[4096];
  size_t len;
  while ((len = read(fd.get(), buf, sizeof(buf))) > 0) {
    file.insert(file.end(), buf, buf + len);
  }
  if (len == -1) {
    *err = "could not read capture log file: " + std::string(strerror(errno));
    return false;
  }

  auto it = file.begin();
  auto end = file.end();
  while (it != end) {
    if (end - it < sizeof(size_t)) {
      *err = "truncated capture log file";
      return false;
    }

    auto size = *reinterpret_cast<const size_t *>(&*it);
    it += sizeof(size_t);

    if (end - it < size) {
      *err = "truncated capture log file";
      return false;
    }

    flatbuffers::Verifier verifier(&*it, size);
    if (!VerifyEntryBuffer(verifier)) {
      *err = "capture log entry did not pass flatbuffer validation";
      return false;
    }

    const auto &entry = *GetEntry(&*it);
    it += size;

    if (entry.trace_request()) {
      const auto &data = *entry.trace_request();

      FileDescriptor trace_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));
      if (trace_fd.get() == -1) {
        *err = "failed to open /dev/null";
        return false;
      }

      trace_request_callback(std::unique_ptr<TracingServer::TraceRequest>(
          new TracingServer::TraceRequest(
              std::move(trace_fd),
              data.pid_to_trace(),
              data.root_thread_id(),
              std::string(data.cwd()->c_str()))));
    }
    if (entry.kd_bufs()) {
      const auto &data = *entry.kd_bufs()->bufs();
      if (data.size() % sizeof(kd_buf) != 0) {
        *err = "truncated kd_buf in capture log file";
        return false;
      }

      const kd_buf *begin = reinterpret_cast<const kd_buf *>(data.data());
      const kd_buf *end = begin + (data.size() / sizeof(kd_buf));
      kd_bufs_callback(begin, end);
    }
  }

  return true;
}

void DebugCaptureLog::writeToFile(void *buf, size_t size) {
  auto written_size = write(_fd.get(), &size, sizeof(size));
  if (written_size != sizeof(size)) {
    fprintf(stderr, "Failed to write to debug capture log file\n");
    abort();
  }
  auto written = write(_fd.get(), buf, size);
  if (written != size) {
    fprintf(stderr, "error: Failed to write to debug capture log file\n");
    abort();
  }
}

}  // namespace shk
