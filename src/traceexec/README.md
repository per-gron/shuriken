# traceexec kernel extension and userspace support library

Shuriken needs to be able to trace the files that are read and written by
commands that it runs in order to do dependency detection. On OS X, there are a
number of ways that this could potentially be achieved, including:

* DTrace: Requires root access (perhaps via a suid root executable), is a bit
  tricky to use because the `cwd` provider is broken on Darwin (only provides
  basename of the working directory, as opposed to the full path), does not work
  well on El Capitan and newer because SIP disallows tracing of executables in
  `/bin`, for example `/bin/sh`.
* FUSE file system that mirrors the real file system, run executables chrooted
  to that file system: The [tup](http://gittup.org/tup/) build system does this.
  Requires FUSE to work, plus root access for chroot (perhaps via a suid root
  executable). Has noticeable performance impact because of the extra kernel
  roundtrips for `read` and `write` system calls (which Shuriken doesn't need
  to intercept anyway).
* Use `(trace 1)` option of `sandbox-exec`: Does not report when files that
  don't exist are requested to be opened. Does not distinguish between files
  that are opened for appending or files that are opened and truncates the file
  first. Unsure if the API is stable; the related `sandbox_init` is deprecated.
  `sandboxd`, which is a process run by root, leaks file descriptors every time
  it is used, seems to need reboot to release them. Reported as #24699352 in
  Apple's bug reporting system. Otherwise quite nice because it can be used
  without root access or special kernel extensions.
* Dynamic instrumentation frameworks, for example
  [DynamoRIO](http://www.dynamorio.org/) and [Frida](http://www.frida.re/): I
  was unable to find an open source option that works on Mac (DynamoRIO doesn't)
  and that supports tracing of syscalls (Frida doesn't). Even if there was one,
  dynamic instrumentation adds significant overhead for each process invocation.
  For example, it takes about three seconds to compile a minimal Hello World C
  application with clang when instrumented by Frida. This overhead applies to
  every compiler invocation. It does not require superuser privileges to use
  this technique, so it is good in that sense.
* `DYLD_INSERT_LIBRARIES` hijacking of file system and process system library
  calls: Does not require superuser privileges to use. However, it is difficult
  to ensure that all relevant system library calls are covered. Furthermore,
  since El Capitan and SIP, it does not work for certain important executables
  such as `/bin/sh`.
* `ptrace` and Mach Exceptions can be used to detect when system calls are made
  from a process. The two mechanisms are similar in how they work. Both have a
  rather high performance overhead because it incurs two extra context switches
  for every syscall, even syscalls that Shuriken does not care about.
* There are some APIs for security auditing that can be used to trace file
  access. I have not investigated them very carefully. I believe they require
  superuser privileges. [I have found places](https://www.synack.com/2015/11/17/monitoring-process-creation-via-the-kernel-part-i/)
  that hint that it is not necessarily easy to track subprocesses with
  sufficient precision using those APIs.
* [File System Events API](https://developer.apple.com/library/mac/documentation/Darwin/Conceptual/FSEvents_ProgGuide/Introduction/Introduction.html),
  listed here because people keep bringing it up as a potential option. It
  cannot be used in this context because it does not report when files are read.
* [TrustedBSD Mandatory Access Control (MAC)](http://www.trustedbsd.org/mac.html)
  is a kernel API. It provides all the information that Shuriken needs in a
  fairly straight forward format. Unlike methods that trace system calls on an
  application level, there is no need to continuously make sure that no new
  syscalls are introduced that allow the program to (even accidentally) escape
  tracing; this burden is on the maintainers of the MAC framework. MAC is what
  powers sandboxing (including the `(trace 1)` mode of `sandbox-exec`) in Mac OS
  X. Being a kernel API, it is not accessible from user space so it requires a
  kernel extension to use directly.

The traceexec kernel extension provides the means for a process to trace its own
file accesses and other accesses with similar results to `(trace 1)` of
`sandbox-exec`. It is implemented using TrustedBSD MAC.

The kernel extension exposes its functionality to userspace via a character
device in `/dev/traceexec`. The kernel extension is accompanied by a userspace
C++ library that makes it easy to use its functionality.
