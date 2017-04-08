#include "to_json.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <util/file_descriptor.h>
#include <util/shktrace.h>

namespace shk {
namespace {

std::string escapeJSON(const std::string &str) {
  std::string result = "\"";

  auto span_begin = str.begin();
  for (auto it = str.begin(), end = str.end(); it != end; ++it) {
    if (*it == '"') {
      result.append(span_begin, it);
      result.push_back('\\');
      span_begin = it;
    }
  }
  result.append(span_begin, str.end());

  result.push_back('"');
  return result;
}

}  // anonymous namespace

bool convertOutputToJson(const std::string &path, std::string *err) {
  FileDescriptor fd(open(path.c_str(), O_RDONLY | O_CLOEXEC));
  if (fd.get() == -1) {
    *err =
        std::string("could not open trace file for reading: ") +
        strerror(errno);
    return false;
  }

  std::vector<uint8_t> file;
  char buf[4096];
  size_t len;
  while ((len = read(fd.get(), buf, sizeof(buf))) > 0) {
    file.insert(file.end(), buf, buf + len);
  }
  if (len == -1) {
    *err = std::string("could not read trace file: ") + strerror(errno);
    return false;
  }

  flatbuffers::Verifier verifier(
      file.data(), file.size());
  if (!VerifyTraceBuffer(verifier)) {
    *err = "trace file did not pass validation";
    return false;
  }

  std::string json = "{";
  auto trace = GetTrace(file.data());

  if (trace->inputs()->size()) {
    json += "\"inputs\":[";
    for (int i = 0; i < trace->inputs()->size(); i++) {
      const auto *input = trace->inputs()->Get(i);
      json += "{\"path\":";
      json += escapeJSON(input->path()->c_str());
      json += ",\"directory_listing\":";
      json += input->directory_listing() ? "true" : "false";
      json += "},";
    }
    json[json.size() - 1] = ']';
    json.push_back(',');
  }

  if (trace->outputs()->size()) {
    json += "\"outputs\":[";
    for (int i = 0; i < trace->outputs()->size(); i++) {
      json += escapeJSON(trace->outputs()->Get(i)->c_str());
      json.push_back(',');
    }
    json[json.size() - 1] = ']';
    json.push_back(',');
  }

  if (trace->errors()->size()) {
    json += "\"errors\":[";
    for (int i = 0; i < trace->errors()->size(); i++) {
      json += escapeJSON(trace->errors()->Get(i)->c_str());
      json.push_back(',');
    }
    json[json.size() - 1] = ']';
    json.push_back(',');
  }

  if (json[json.size() - 1] == ',') {
    json.resize(json.size() - 1);
  }
  json += "}";

  fd.reset(open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC));
  if (fd.get() == -1) {
    *err =
        std::string("could not open trace file for writing: ") +
        strerror(errno);
    return false;
  }

  if (write(fd.get(), json.data(), json.size()) == -1) {
    *err =
        std::string("could not write trace JSON: ") +
        strerror(errno);
    return false;
  }

  return true;
}


}  // namespace shk
