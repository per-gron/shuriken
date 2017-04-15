// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "edit_distance.h"

#include <algorithm>

namespace shk {

int editDistance(
    string_view s1,
    string_view s2,
    bool allow_replacements,
    int max_edit_distance) {
  // The algorithm implemented below is the "classic"
  // dynamic-programming algorithm for computing the Levenshtein
  // distance, which is described here:
  //
  //   http://en.wikipedia.org/wiki/Levenshtein_distance
  //
  // Although the algorithm is typically described using an m x n
  // array, only one row plus one element are used at a time, so this
  // implementation just keeps one vector for the row.  To update one entry,
  // only the entries to the left, top, and top-left are needed.  The left
  // entry is in row[x-1], the top entry is what's in row[x] from the last
  // iteration, and the top-left entry is stored in previous.
  int m = s1.size();
  int n = s2.size();

  std::vector<int> row(n + 1);
  for (int i = 1; i <= n; ++i) {
    row[i] = i;
  }

  for (int y = 1; y <= m; ++y) {
    row[0] = y;
    int best_this_row = row[0];

    int previous = y - 1;
    for (int x = 1; x <= n; ++x) {
      int old_row = row[x];
      if (allow_replacements) {
        row[x] = std::min(
            previous + (s1.data()[y - 1] == s2.data()[x - 1] ? 0 : 1),
            std::min(row[x - 1], row[x]) + 1);
      }
      else {
        if (s1.data()[y - 1] == s2.data()[x - 1]) {
          row[x] = previous;
        } else {
          row[x] = std::min(row[x - 1], row[x]) + 1;
        }
      }
      previous = old_row;
      best_this_row = std::min(best_this_row, row[x]);
    }

    if (max_edit_distance && best_this_row > max_edit_distance) {
      return max_edit_distance + 1;
    }
  }

  return row[n];
}

const char *spellcheckStringV(
    const std::string &text,
    const std::vector<const char*> &words) {
  const bool kAllowReplacements = true;
  const int kMaxValidEditDistance = 3;

  int min_distance = kMaxValidEditDistance + 1;
  const char* result = NULL;
  for (auto i = words.begin(); i != words.end(); ++i) {
    int distance = editDistance(*i, text, kAllowReplacements,
                                kMaxValidEditDistance);
    if (distance < min_distance) {
      min_distance = distance;
      result = *i;
    }
  }
  return result;
}

const char *spellcheckString(const char *text, ...) {
  // Note: This takes a const char* instead of a string& because using
  // va_start() with a reference parameter is undefined behavior.
  va_list ap;
  va_start(ap, text);
  std::vector<const char*> words;
  const char* word;
  while ((word = va_arg(ap, const char*)))
    words.push_back(word);
  va_end(ap);
  return spellcheckStringV(text, words);
}

}  // namespace shk
