#include "version.h"

#include <errno.h>
#include <sys/ioctl.h>
#include <sys/ioccom.h> // Needed to be able to #include kern_control.h
#include <sys/types.h>  // Needed to be able to #include kern_control.h
#include <sys/kern_control.h>
#include <sys/socket.h>
#include <sys/sys_domain.h>

#include <util/raii_helper.h>

#include "traceexec_cmds.h"

namespace traceexec {

using FdHelper = util::RAIIHelper<int, int, close, -1>;

Version getKextVersion() throw(TraceexecError) {
  Version version;

  const auto fd = FdHelper(socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL));
  if (!fd) {
    throw TraceexecError(
        std::string("failed to open socket: ") + strerror(errno));
  }

  sockaddr_ctl addr;
  bzero(&addr, sizeof(addr)); // sets the sc_unit field to 0
  addr.sc_len = sizeof(addr);
  addr.sc_family = AF_SYSTEM;
  addr.ss_sysaddr = AF_SYS_CONTROL;
  {
    ctl_info info;
    memset(&info, 0, sizeof(info));
    strncpy(info.ctl_name, kTraceexecControlName, sizeof(info.ctl_name));
    if (ioctl(fd.get(), CTLIOCGINFO, &info)) {
      throw TraceexecError("traceexec kernel extension not loaded\n");
    }
    addr.sc_id = info.ctl_id;
    addr.sc_unit = 0;
  }

  int result = connect(fd.get(), (struct sockaddr *)&addr, sizeof(addr));
  if (result) {
    fprintf(stderr, "connect failed %d\n", result);
  }

  if (!result) {
    socklen_t len = sizeof(version);
    result = getsockopt(fd.get(), SYSPROTO_CONTROL, kTraceexecGetVersion, &version, &len);
    if (result) {
      fprintf(stderr, "setsockopt failed on kTraceexecGetVersion call - result was %d\n", result);
    }
  }

  return version;
}

}  // namespace traceexec
