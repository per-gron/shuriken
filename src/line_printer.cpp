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

#include "line_printer.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <sys/time.h>
#endif

namespace detail {

std::string elideMiddle(const std::string &str, size_t width) {
  const int kMargin = 3;  // Space for "...".
  auto result = str;
  if (result.size() + kMargin > width) {
    size_t elide_size = (width - kMargin) / 2;
    result = result.substr(0, elide_size)
      + "..."
      + result.substr(result.size() - elide_size, elide_size);
  }
  return result;
}

}  // namespace detail

LinePrinter::LinePrinter() : _have_blank_line(true), _console_locked(false) {
#ifndef _WIN32
  const char* term = getenv("TERM");
  _smart_terminal = isatty(1) && term && std::string(term) != "dumb";
#else
  // Disable output buffer.  It'd be nice to use line buffering but
  // MSDN says: "For some systems, [_IOLBF] provides line
  // buffering. However, for Win32, the behavior is the same as _IOFBF
  // - Full Buffering."
  setvbuf(stdout, NULL, _IONBF, 0);
  _console = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  _smart_terminal = GetConsoleScreenBufferInfo(_console, &csbi);
#endif
}

void LinePrinter::print(std::string to_print, LineType type) {
  if (_console_locked) {
    _line_buffer = to_print;
    _line_type = type;
    return;
  }

  if (_smart_terminal) {
    printf("\r");  // Print over previous line, if any.
    // On Windows, calling a C library function writing to stdout also handles
    // pausing the executable when the "Pause" key or Ctrl-S is pressed.
  }

  if (_smart_terminal && type == ELIDE) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(_console, &csbi);

    to_print = detail::elideMiddle(to_print, static_cast<size_t>(csbi.dwSize.X));
    // We don't want to have the cursor spamming back and forth, so instead of
    // printf use WriteConsoleOutput which updates the contents of the buffer,
    // but doesn't move the cursor position.
    COORD buf_size = { csbi.dwSize.X, 1 };
    COORD zero_zero = { 0, 0 };
    SMALL_RECT target = {
      csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y,
      static_cast<SHORT>(csbi.dwCursorPosition.X + csbi.dwSize.X - 1),
      csbi.dwCursorPosition.Y
    };
    vector<CHAR_INFO> char_data(csbi.dwSize.X);
    for (size_t i = 0; i < static_cast<size_t>(csbi.dwSize.X); ++i) {
      char_data[i].Char.AsciiChar = i < to_print.size() ? to_print[i] : ' ';
      char_data[i].Attributes = csbi.wAttributes;
    }
    WriteConsoleOutput(_console, &char_data[0], buf_size, zero_zero, &target);
#else
    // Limit output to width of the terminal if provided so we don't cause
    // line-wrapping.
    winsize size;
    if ((ioctl(0, TIOCGWINSZ, &size) == 0) && size.ws_col) {
      to_print = detail::elideMiddle(to_print, size.ws_col);
    }
    printf("%s", to_print.c_str());
    printf("\x1B[K");  // Clear to end of line.
    fflush(stdout);
#endif

    _have_blank_line = false;
  } else {
    printf("%s\n", to_print.c_str());
  }
}

void LinePrinter::printOrBuffer(const char *data, size_t size) {
  if (_console_locked) {
    _output_buffer.append(data, size);
  } else {
    // Avoid printf and C strings, since the actual output might contain null
    // bytes like UTF-16 does (yuck).
    fwrite(data, 1, size, stdout);
  }
}

void LinePrinter::printOnNewLine(const std::string &to_print) {
  if (_console_locked && !_line_buffer.empty()) {
    _output_buffer.append(_line_buffer);
    _output_buffer.append(1, '\n');
    _line_buffer.clear();
  }
  if (!_have_blank_line) {
    printOrBuffer("\n", 1);
  }
  if (!to_print.empty()) {
    printOrBuffer(&to_print[0], to_print.size());
  }
  _have_blank_line = to_print.empty() || *to_print.rbegin() == '\n';
}

void LinePrinter::setConsoleLocked(bool locked) {
  if (locked == _console_locked)
    return;

  if (locked)
    printOnNewLine("");

  _console_locked = locked;

  if (!locked) {
    printOnNewLine(_output_buffer);
    if (!_line_buffer.empty()) {
      print(_line_buffer, _line_type);
    }
    _output_buffer.clear();
    _line_buffer.clear();
  }
}
