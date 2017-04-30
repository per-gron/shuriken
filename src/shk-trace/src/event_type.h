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

#pragma once

namespace shk {

enum class EventType {
  /**
   * Read is used when a program performs an operation that provides
   * information about a given file, including both metadata and actual file
   * contents.
   *
   * When the path points to a directory, it only means that the metadata of
   * the directory has been accessed. If the actual directory entries were
   * accessed, the EventType ReadDirectory is used.
   */
  READ,

  /**
   * ReadDirectory is used when a program performs an operation that lists the
   * entries of a given directory, including metadata.
   */
  READ_DIRECTORY,

  /**
   * Write is used when a program performs an operation that modifies a file
   * but that could potentially leave parts of the previous file contents.
   */
  WRITE,

  /**
   * Create is used when a program creates a file, or when it entirely
   * overwrites the contents of a file.
   */
  CREATE,

  /**
   * Delete is used when a program deletes a file. Because deleting a file
   * exposes information to the program about whether the file exists, Delete
   * also implies Read. (Delete+Create is used when moving files, so Delete
   * truly implies that the file contents matter as well.)
   */
  DELETE,

  /**
   * FatalError events mean that the Tracer has failed. It could be that it's
   * seen an event that the Tracer does not understand, and it doesn't know
   * which files may have been read or written because of it. This happens for
   * legacy Carbon File Manager system calls. It can also be because of
   * internal errors in the tracer.
   *
   * For FatalError events, the path provided is undefined and has no
   * meaning.
   */
  FATAL_ERROR
};

}  // namespace shk
