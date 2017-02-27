#pragma once

#include <vector>

#include "kdebug.h"

namespace shk {

class Tracer {
 public:
  Tracer();

  int run();

 private:
  void loop();

  std::vector<kd_buf> _event_buffer;
};

}
