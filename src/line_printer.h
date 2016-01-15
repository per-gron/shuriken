// Copyright 2013 Google Inc. All Rights Reserved.
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

#include <stddef.h>
#include <string>

namespace detail {

/**
 * Elide the given string @a str with '...' in the middle if the length
 * exceeds @a width.
 */
std::string elideMiddle(const std::string &str, size_t width);

}  // namespace detail

/**
 * Prints lines of text, possibly overprinting previously printed lines
 * if the terminal supports it.
 */
struct LinePrinter {
  LinePrinter();

  bool isSmartTerminal() const { return _smart_terminal; }
  void setSmartTerminal(bool smart) { _smart_terminal = smart; }

  enum LineType {
    FULL,
    ELIDE
  };
  /**
   * Overprints the current line. If type is ELIDE, elides to_print to fit on
   * one line.
   */
  void print(std::string to_print, LineType type);

  /**
   * Prints a string on a new line, not overprinting previous output.
   */
  void printOnNewLine(const std::string& to_print);

  /**
   * Lock or unlock the console.  Any output sent to the LinePrinter while the
   * console is locked will not be printed until it is unlocked.
   */
  void setConsoleLocked(bool locked);

 private:
  /**
   * Whether we can do fancy terminal control codes.
   */
  bool _smart_terminal;

  /**
   * Whether the caret is at the beginning of a blank line.
   */
  bool _have_blank_line;

  /**
   * Whether console is locked.
   */
  bool _console_locked;

  /**
   * Buffered current line while console is locked.
   */
  std::string _line_buffer;

  /**
   * Buffered line type while console is locked.
   */
  LineType _line_type;

  /**
   * Buffered console output while console is locked.
   */
  std::string _output_buffer;

#ifdef _WIN32
  void *_console;
#endif

  /**
   * Print the given data to the console, or buffer it if it is locked.
   */
  void printOrBuffer(const char *data, size_t size);
};
