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

#ifdef _WIN32
#include "win32port.h"
#else
#include <stdint.h>
#endif

#include <string>
#include <vector>

#include "string_view.h"

#ifdef _MSC_VER
#define NORETURN __declspec(noreturn)
#else
#define NORETURN __attribute__((noreturn))
#endif

namespace shk {

/**
 * Log a fatal message and exit.
 */
NORETURN void fatal(const char *msg, ...);

/**
 * Log a warning message.
 */
void warning(const char *msg, ...);

/**
 * Log an error message.
 */
void error(const char *msg, ...);

/**
 * Appends |input| to |*result|, escaping according to the whims of either
 * Bash, or Win32's CommandLineToArgvW().
 * Appends the string directly to |result| without modification if we can
 * determine that it contains no problematic characters.
 */
void getShellEscapedString(nt_string_view input, std::string *result);
void getWin32EscapedString(nt_string_view input, std::string *result);

/**
 * Mark a file descriptor to not be inherited on exec()s.
 */
void setCloseOnExec(int fd);

/**
 * Removes all Ansi escape codes (http://www.termsys.demon.co.uk/vtansi.htm).
 */
std::string stripAnsiEscapeCodes(const std::string &in);

/**
 * @return the number of processors on the machine.  Useful for an initial
 * guess for how many jobs to run in parallel.  @return 0 on error.
 */
int getProcessorCount();

/**
 * Choose a default value for the -j (parallelism) flag.
 */
int guessParallelism();

std::string getWorkingDir();

/**
 * @return the load average of the machine. A negative value is returned
 * on error.
 */
double getLoadAverage();

#ifdef _MSC_VER
#define snprintf _snprintf
#define fileno _fileno
#define unlink _unlink
#define chdir _chdir
#define strtoull _strtoui64
#define getcwd _getcwd
#define PATH_MAX _MAX_PATH
#endif

#ifdef _WIN32
/**
 * Convert the value returned by getLastError() into a string.
 */
std::string getLastErrorString();

/**
 * Calls fatal() with a function name and getLastErrorString.
 */
NORETURN void win32Fatal(const char *function);
#endif

}  // namespace shk
