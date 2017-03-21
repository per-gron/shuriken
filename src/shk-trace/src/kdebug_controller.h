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

  virtual void setNumbufs(int nbufs) = 0;
  virtual void setFilter() = 0;
  virtual kbufinfo_t getBufinfo() = 0;
  virtual void enable(bool enabled) = 0;
  virtual void setup() = 0;
  virtual void teardown() = 0;
  virtual size_t readBuf(kd_buf *bufs, size_t num_bufs) = 0;
};

std::unique_ptr<KdebugController> makeKdebugController();

}  // namespace shk
