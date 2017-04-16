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

#include "terminal_build_status.h"

#include "manifest/step.h"
#include "status/line_printer.h"
#include "util.h"

namespace shk {
namespace {

class TerminalBuildStatus : public BuildStatus {
 public:
  TerminalBuildStatus(
      bool verbose,
      int parallelism,
      int total_steps,
      const char *progress_status_format)
      : _verbose(verbose),
        _total_steps(total_steps),
        _progress_status_format(progress_status_format),
        _current_rate(parallelism) {}

  ~TerminalBuildStatus() override {
    _printer.setConsoleLocked(false);
    _printer.printOnNewLine("");
  }

  void stepStarted(const Step &step) override {
    ++_started_steps;

    const auto use_console = isConsolePool(step.poolName());

    if (use_console || _printer.isSmartTerminal()) {
      printStatus(step);
    }

    if (use_console) {
      _printer.setConsoleLocked(true);
    }
  }

  void stepFinished(
      const Step &step,
      bool success,
      const std::string &output) override {
    ++_finished_steps;

    const auto use_console = isConsolePool(step.poolName());

    if (use_console) {
      _printer.setConsoleLocked(false);
    }

    if (!use_console) {
      printStatus(step);
    }

    // Print the command that is spewing before printing its output.
    if (!success) {
      _printer.printOnNewLine("FAILED: " + std::string(step.command()) + "\n");
    }

    if (!output.empty()) {
      // Shuriken sets stdout and stderr of subprocesses to a pipe, to be able
      // to check if the output is empty. Some compilers, e.g. clang, check
      // isatty(stderr) to decide if they should print colored output.
      // To make it possible to use colored output with Shuriken, subprocesses
      // should be run with a flag that forces them to always print color escape
      // codes. To make sure these escape codes don't show up in a file if
      // Shuriken's output is piped to a file, Shuriken strips ansi escape codes
      // again if it's not writing to a |_smart_terminal|. (Launching
      // subprocesses in pseudo ttys doesn't work because there are only a few
      // hundred available on some systems, and Shuriken can launch thousands of
      // parallel compile commands.)
      //
      // TODO: There should be a flag to disable escape code stripping.
      std::string final_output;
      if (!_printer.isSmartTerminal()) {
        final_output = stripAnsiEscapeCodes(output);
      } else {
        final_output = output;
      }
      _printer.printOnNewLine(final_output);
    }
  }

  /**
   * Format the progress status string by replacing the placeholders.
   * See the user manual for more information about the available
   * placeholders.
   *
   * @param progress_status_format The format of the progress status.
   */
  std::string formatProgressStatus(const char *progress_status_format) {
    std::string out;
    char buf[32];
    int percent;
    for (const char *s = progress_status_format; *s != '\0'; ++s) {
      if (*s == '%') {
        ++s;
        switch (*s) {
        case '%':
          out.push_back('%');
          break;

          // Started steps.
        case 's':
          snprintf(buf, sizeof(buf), "%d", _started_steps);
          out += buf;
          break;

          // Total steps.
        case 't':
          snprintf(buf, sizeof(buf), "%d", _total_steps);
          out += buf;
          break;

          // Running steps.
        case 'r':
          snprintf(buf, sizeof(buf), "%d", _started_steps - _finished_steps);
          out += buf;
          break;

          // Unstarted steps.
        case 'u':
          snprintf(buf, sizeof(buf), "%d", _total_steps - _started_steps);
          out += buf;
          break;

          // Finished steps.
        case 'f':
          snprintf(buf, sizeof(buf), "%d", _finished_steps);
          out += buf;
          break;

          // Overall finished steps per second.
        case 'o':
          _overall_rate.updateRate(_finished_steps);
          snprinfRate(_overall_rate.rate(), buf, "%.1f");
          out += buf;
          break;

          // Current rate, average over the last '-j' jobs.
        case 'c':
          _current_rate.updateRate(_finished_steps);
          snprinfRate(_current_rate.rate(), buf, "%.1f");
          out += buf;
          break;

          // Percentage
        case 'p':
          percent = (100 * _started_steps) / _total_steps;
          snprintf(buf, sizeof(buf), "%3i%%", percent);
          out += buf;
          break;

        case 'e': {
          double elapsed = _overall_rate.elapsed();
          snprintf(buf, sizeof(buf), "%.3f", elapsed);
          out += buf;
          break;
        }

        default:
          fatal("unknown placeholder '%%%c' in $NINJA_STATUS", *s);
          return "";
        }
      } else {
        out.push_back(*s);
      }
    }

    return out;
  }

 private:
  void printStatus(const Step &step) {
    nt_string_view to_print = step.description();
    if (to_print.empty() || _verbose) {
      to_print = step.command();
    }

    if (_finished_steps == 0) {
      _overall_rate.restart();
      _current_rate.restart();
    }

    _printer.print(
        formatProgressStatus(_progress_status_format) + std::string(to_print),
        _verbose ? LinePrinter::FULL : LinePrinter::ELIDE);
  }

  template<size_t S>
  void snprinfRate(double rate, char(&buf)[S], const char* format) const {
    if (rate == -1) {
      snprintf(buf, S, "?");
    } else {
      snprintf(buf, S, format, rate);
    }
  }

  const bool _verbose;

  int _started_steps = 0;
  int _finished_steps = 0;
  const int _total_steps;

  /**
   * Prints progress output.
   */
  LinePrinter _printer;

  /**
   * The custom progress status format to use.
   */
  const char * const _progress_status_format;

  detail::RateInfo _overall_rate;
  detail::SlidingRateInfo _current_rate;
};

}  // anonymous namespace

std::unique_ptr<BuildStatus> makeTerminalBuildStatus(
    bool verbose,
    int parallelism,
    int total_steps,
    const char *progress_status_format) {
  return std::unique_ptr<BuildStatus>(new TerminalBuildStatus(
      verbose,
      parallelism,
      total_steps,
      progress_status_format));
}

}  // namespace shk
