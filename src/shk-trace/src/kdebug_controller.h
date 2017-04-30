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

#include <stdexcept>
#include <memory>

#include <sys/sysctl.h>

#include "kdebug.h"

namespace shk {

/**
 * KdebugController objects expose a low-level interface to kdebug, only thick
 * enough to facilitate unit testing of classes that use it.
 */
class KdebugController {
 public:
  virtual ~KdebugController() = default;

  virtual void start(int nbufs) = 0;
  virtual size_t readBuf(kd_buf *bufs) = 0;
};

std::unique_ptr<KdebugController> makeKdebugController();

}  // namespace shk
