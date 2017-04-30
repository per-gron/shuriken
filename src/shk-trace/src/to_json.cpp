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

void writeJsonPathList(
    const char *name,
    const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> &paths,
    std::string *json) {
  if (paths.size()) {
    json->push_back('"');
    *json += name;
    *json += "\":[";
    for (int i = 0; i < paths.size(); i++) {
      *json += escapeJSON(paths.Get(i)->c_str());
      json->push_back(',');
    }
    (*json)[json->size() - 1] = ']';
    json->push_back(',');
  }
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

  writeJsonPathList("inputs", *trace->inputs(), &json);
  writeJsonPathList("outputs", *trace->outputs(), &json);
  writeJsonPathList("errors", *trace->errors(), &json);

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
