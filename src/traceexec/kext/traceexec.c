#include <mach/mach_types.h>
#include <libkern/libkern.h>

kern_return_t traceexec_start(kmod_info_t * ki, void *d);
kern_return_t traceexec_stop(kmod_info_t *ki, void *d);

kern_return_t traceexec_start(kmod_info_t * ki, void *d)
{
    printf("TRACEEXEC STARTED\n");
    return KERN_SUCCESS;
}

kern_return_t traceexec_stop(kmod_info_t *ki, void *d)
{
    return KERN_SUCCESS;
}
