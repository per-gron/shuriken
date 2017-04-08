#pragma once

#include <string>

namespace shk {

/**
 * Read a trace flatbuffer file and overwrite it with the same contents but in
 * JSON. This is useful for debugging.
 */
bool convertOutputToJson(const std::string &path, std::string *err);

}  // namespace shk
