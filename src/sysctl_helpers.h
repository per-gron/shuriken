#pragma once

#include <stdexcept>

#include <sys/sysctl.h>

#include "kdebug.h"

int get_num_cpus() throw(std::runtime_error);

void set_kdebug_numbufs(int nbufs);

int kdebugFilterIndex(int klass, int subclass);

void set_kdebug_filter();

kbufinfo_t get_kdebug_bufinfo();

void enable_kdebug(bool enabled);

void kdebug_exclude_pid(int pid, bool enable_exclusion);

void kdebug_setup();

void kdebug_teardown();

size_t kdebug_read_buf(kd_buf *bufs, size_t num_bufs);
