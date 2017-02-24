/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * "Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 1.0 (the 'License').  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License."
 *
 * @APPLE_LICENSE_HEADER_END@
 */

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
