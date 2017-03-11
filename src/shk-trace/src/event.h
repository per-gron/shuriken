#pragma once

namespace shk {

enum class EventType {
  /**
   * READ is used when a program performs an operation that provides
   * information about a given file, including both metadata and actual file
   * contents.
   */
  READ,
  /**
   * WRITE is used when a program performs an operation that modifies a file
   * but that could potentially leave parts of the previous file contents.
   */
  WRITE,
  /**
   * CREATE is used when a program creates a file, or when it entirely
   * overwrites the contents of a file.
   */
  CREATE,
  DELETE,
  /**
   * FATAL_ERROR events mean that the Tracer has failed. It could be that it's
   * seen an event that the Tracer does not understand, and it doesn't know
   * which files may have been read or written because of it. This happens for
   * legacy Carbon File Manager system calls. It can also be because of
   * internal errors in the tracer.
   *
   * For FATAL_ERROR events, the path provided is undefined and has no
   * meaning.
   */
  FATAL_ERROR
};

}  // namespace shk
