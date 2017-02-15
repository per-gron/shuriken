#pragma once

#include <bitset>
#include <stdexcept>

#include <sys/sysctl.h>

int get_num_cpus() throw(std::runtime_error) {
  int num_cpus;
  size_t len = sizeof(num_cpus);
  static int name[] = { CTL_HW, HW_NCPU, 0 };
  if (sysctl(name, 2, &num_cpus, &len, NULL, 0) < 0) {
    throw std::runtime_error("Failed to get number of CPUs");
  }
  return num_cpus;
}

void set_kdebug_numbufs(int nbufs) {
  static size_t len = 0;
  static int name_1[] = { CTL_KERN, KERN_KDEBUG, KERN_KDSETBUF, nbufs, 0, 0 };
  if (sysctl(name_1, 4, nullptr, &len, nullptr, 0)) {
    throw std::runtime_error("Failed KERN_KDSETBUF sysctl");
  }

  static int name_2[] = { CTL_KERN, KERN_KDEBUG, KERN_KDSETUP, 0, 0, 0 };
  if (sysctl(name_2, 3, nullptr, &len, nullptr, 0)) {
    throw std::runtime_error("Failed KERN_KDSETUP sysctl");
  }
}

int kdebugFilterIndex(int klass, int subclass) {
  return ((klass & 0xff) << 8) | (subclass & 0xff);
}

void set_kdebug_filter() {
  std::bitset<KDBG_TYPEFILTER_BITMAP_SIZE * 8> filter{};

  filter.set(kdebugFilterIndex(DBG_TRACE, DBG_TRACE_DATA));
  filter.set(kdebugFilterIndex(DBG_TRACE, DBG_TRACE_STRING));
  filter.set(kdebugFilterIndex(DBG_MACH, DBG_MACH_EXCP_SC));
  filter.set(kdebugFilterIndex(DBG_FSYSTEM, DBG_FSRW));
  filter.set(kdebugFilterIndex(DBG_FSYSTEM, DBG_BOOTCACHE));
  filter.set(kdebugFilterIndex(DBG_BSD, DBG_BSD_EXCP_SC));
  filter.set(kdebugFilterIndex(DBG_BSD, DBG_BSD_PROC));
  filter.set(kdebugFilterIndex(DBG_BSD, DBG_BSD_SC_EXTENDED_INFO));
  filter.set(kdebugFilterIndex(DBG_BSD, DBG_BSD_SC_EXTENDED_INFO2));
  filter.set(kdebugFilterIndex(FILEMGR_CLASS, 0)); // Carbon File Manager
  filter.set(kdebugFilterIndex(FILEMGR_CLASS, 1)); // Carbon File Manager

  static int name[] = { CTL_KERN, KERN_KDEBUG, KERN_KDSET_TYPEFILTER };
  size_t len = filter.size() / 8;
  if (sysctl(name, 3, &filter, &len, NULL, 0)) {
    throw std::runtime_error("Failed KERN_KDSET_TYPEFILTER sysctl");
  }
}
