#pragma once

#include <libc.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "event_info.h"
#include "kdebug.h"
#include "kdebug_controller.h"
#include "syscall_constants.h"
#include "syscall_tables.h"

namespace shk {

class Tracer {
 public:
  Tracer(int num_cpus, std::unique_ptr<KdebugController> &&kdebug_ctrl);

  int run();

 private:
  void loop();

  void set_enable(bool enabled);
  void set_remove();
  uint64_t sample_sc(std::vector<kd_buf> &event_buffer);
  void enter_event_now(uintptr_t thread, int type, kd_buf *kd, const char *name);
  void enter_event(uintptr_t thread, int type, kd_buf *kd, const char *name);
  void enter_illegal_event(uintptr_t thread, int type);
  void exit_event(
      uintptr_t thread,
      int type,
      uintptr_t arg1,
      uintptr_t arg2,
      uintptr_t arg3,
      uintptr_t arg4,
      const bsd_syscall &syscall);
  void format_print(
      event_info *ei,
      uintptr_t thread,
      int type,
      uintptr_t arg1,
      uintptr_t arg2,
      uintptr_t arg3,
      uintptr_t arg4,
      const bsd_syscall &syscall,
      const char *pathname /* nullable */);
  void read_command_map(const kbufinfo_t &bufinfo);
  void create_map_entry(uintptr_t thread, int pid, char *command);
  void init_arguments_buffer();
  int get_real_command_name(int pid, char *cbuf, int csize);

  struct threadmap_entry {
    unsigned int tm_setsize = 0; // This is a bit count
    unsigned long *tm_setptr = nullptr;  // File descriptor bitmap
    char tm_command[MAXCOMLEN + 1];
  };

  std::vector<kd_buf> _event_buffer;

  const std::unique_ptr<KdebugController> _kdebug_ctrl;

  std::unordered_map<uintptr_t, threadmap_entry> _threadmap;
  std::unordered_map<uint64_t, std::string> _vn_name_map;
  event_info_map _ei_map;

  int _need_new_map = 1;

  char *_arguments = 0;
  int _argmax = 0;

  int _trace_enabled = 0;
};

}
