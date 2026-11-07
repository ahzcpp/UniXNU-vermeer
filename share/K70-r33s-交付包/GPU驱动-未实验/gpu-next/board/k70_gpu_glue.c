/* SPDX-License-Identifier: MIT */
/*
 * K70 (vermeer) GPU build: the sheng board hooks the Adreno740 driver and GpuPowerProbe.cpp call, which the
 * K70 kernel does not have. Each one keeps the driver's own "facility absent" behaviour:
 *
 *  - boot_latency_*: sheng's iPadOS 27 latency tracer (BootLatencyTrace.c) is not part of the K70 kernel; off.
 *  - boot_progress_ext_wide: the two wide GPU status rows sheng's BootProgress.c shows; kept here so a later
 *    K70 observer can print them (GpuPowerProbe writes "R ..." reports into them).
 *  - boot_log_export_put: sheng's UFS log export (BootLog.c); refused, the render server keeps its caches local.
 *  - native_pmap_user_mappings: an XNU patch of sheng's 27 series; reports no user mapping (the driver's own
 *    weak-import fallback when the kernel lacks it).
 *  - native_gop_handoff_*: the NativeGOPPresenter user-space scanout hand-off. The K70 display stack is
 *    NativeLegacyDisplay + K70DisplayProvider, so the hand-off is unsupported unless NativeGOPPresenter is linked
 *    instead of these two (see README-gpu.md).
 */
#include <stdint.h>
#include <stddef.h>

int boot_latency_enabled(void);
void boot_latency_record(uint32_t kind, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f);
int boot_log_export_put(uint32_t generation, uint32_t total, uint32_t offset, const void *data, uint32_t size);
unsigned int native_pmap_user_mappings(uint32_t ppnum, void **first_pmap, uint64_t *first_va, char *first_name);
int native_gop_handoff_call(uint32_t op, void *task, const void *in, size_t in_size, void *out, size_t out_size);
void *native_gop_handoff_memory(void);

char boot_progress_ext_wide[2][112];

int
boot_latency_enabled(void)
{
	return 0;
}

void
boot_latency_record(uint32_t kind, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
	(void)kind; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
}

int
boot_log_export_put(uint32_t generation, uint32_t total, uint32_t offset, const void *data, uint32_t size)
{
	(void)generation; (void)total; (void)offset; (void)data; (void)size;
	return 45;      /* ENOTSUP */
}

unsigned int
native_pmap_user_mappings(uint32_t ppnum, void **first_pmap, uint64_t *first_va, char *first_name)
{
	(void)ppnum;
	if (first_pmap) {
		*first_pmap = NULL;
	}
	if (first_va) {
		*first_va = 0;
	}
	if (first_name) {
		first_name[0] = 0;
	}
	return 0;
}

int
native_gop_handoff_call(uint32_t op, void *task, const void *in, size_t in_size, void *out, size_t out_size)
{
	(void)op; (void)task; (void)in; (void)in_size; (void)out; (void)out_size;
	return (int)0xe00002c7;   /* kIOReturnUnsupported */
}

void *
native_gop_handoff_memory(void)
{
	return NULL;
}
