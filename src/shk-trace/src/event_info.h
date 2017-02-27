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

#include <unordered_map>

#include "kdebug.h"

#define MAX_PATHNAMES 3
#define MAX_SCALL_PATHNAMES 2

struct lookup {
  uintptr_t pathname[NUMPARMS + 1]; /* add room for null terminator */
};

struct event_info {
  event_info()
      : pathptr(&lookups[0].pathname[0]) {
    for (int i = 0; i < MAX_PATHNAMES; i++) {
      lookups[i].pathname[0] = 0;
    }
  }

  uintptr_t child_thread = 0;
  int pid = 0;
  int type = 0;
  int arg1 = 0;
  int arg2 = 0;
  int arg3 = 0;
  int arg4 = 0;
  int arg5 = 0;
  int arg6 = 0;
  int arg7 = 0;
  int arg8 = 0;
  uint64_t vnodeid = 0;
  uintptr_t *pathptr = nullptr;
  int pn_scall_index = 0;
  int pn_work_index = 0;
  struct lookup lookups[MAX_PATHNAMES];
};

class event_info_map {
  struct ei_hash {
    using argument_type = std::pair<uintptr_t, int>;
    using result_type = std::size_t;

    result_type operator()(const argument_type &f) const {
      return
          std::hash<uintptr_t>()(f.first) ^
          std::hash<int>()(f.second);
    }
  };

  using map = std::unordered_map<std::pair<uintptr_t, int>, event_info, ei_hash>;

 public:
  using iterator = map::iterator;

  void clear() {
    _map.clear();
    _last_event_map.clear();
  }

  void erase(iterator iter) {
    auto thread = iter->first.first;
    auto type = iter->first.second;

    auto last_event_it = _last_event_map.find(thread);
    if (last_event_it != _last_event_map.end() && last_event_it->second == type) {
      _last_event_map.erase(last_event_it);
    }

    _map.erase(iter);
  }

  iterator add_event(uintptr_t thread, int type) {
    _map[std::make_pair(thread, type)] = event_info();
    _last_event_map[thread] = type;
    return _map.find(std::make_pair(thread, type));
  }

  iterator find(uintptr_t thread, int type) {
    return _map.find(std::make_pair(thread, type));
  }

  iterator find_last(uintptr_t thread) {
    auto it = _last_event_map.find(thread);
    return it == _last_event_map.end() ?
        end() :
        _map.find(std::make_pair(thread, it->second));
  }

  iterator end() {
    return _map.end();
  }

 private:
  map _map;
  // Map from thread id to last event type for that thread
  std::unordered_map<uintptr_t, int> _last_event_map;
};
