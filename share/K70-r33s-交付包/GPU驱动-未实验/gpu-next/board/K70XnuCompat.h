/* SPDX-License-Identifier: MIT */
/*
 * K70 (vermeer) iPadOS 17.7.11 kernel: compatibility for Adreno740 sources written against a newer XNU.
 *
 * The K70 kernel tree's XNU defines PRIO_DARWIN_GPU_ALLOW/DENY only; it has no per-task GPU role
 * (task_get_gpu_role, PRIO_DARWIN_GPU_BACKGROUND/UTILITY/...). Without a role the driver cannot tell a
 * background renderer from the visible UI, so every submission takes the queue's foreground priority
 * (the documented "unspecified role" behaviour). Force-included into the C++ driver objects only.
 */
#ifndef K70_XNU_COMPAT_H
#define K70_XNU_COMPAT_H
#ifdef __cplusplus
#include <mach/mach_types.h>
#include <sys/resource.h>
#ifndef PRIO_DARWIN_GPU_BACKGROUND
#define K70_XNU_NO_GPU_ROLE 1
#define PRIO_DARWIN_GPU_UNKNOWN 0x1000
#define PRIO_DARWIN_GPU_BACKGROUND 0x3
#define PRIO_DARWIN_GPU_UTILITY 0x4
#define PRIO_DARWIN_GPU_UI_NON_FOCAL 0x5
#define PRIO_DARWIN_GPU_UI 0x6
#define PRIO_DARWIN_GPU_UI_FOCAL 0x7
static inline int task_get_gpu_role(task_t task)
{
    (void)task;
    return PRIO_DARWIN_GPU_UNKNOWN;
}
#endif
#endif
#endif
