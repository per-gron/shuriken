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

#pragma once

#include <vector>

#include "string_piece.h"

namespace shk {

int editDistance(
    const StringPiece &s1,
    const StringPiece &s2,
    bool allow_replacements = true,
    int max_edit_distance = 0);

/**
 * Given a misspelled string and a list of correct spellings, returns
 * the closest match or NULL if there is no close enough match.
 */
const char* spellcheckStringV(
    const std::string &text,
    const std::vector<const char*>& words);

/**
 * Like SpellcheckStringV, but takes a NULL-terminated list.
 */
const char* spellcheckString(const char *text, ...);

}  // namespace shk
