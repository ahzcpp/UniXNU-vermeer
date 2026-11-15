/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef K70_NVRAM_RO_EXPORTS399_H
#define K70_NVRAM_RO_EXPORTS399_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
class IOService;
extern "C" {
#endif
bool k70_nvram_ro_active399(void);
bool k70_nvram_ro_for_provider399(const void *provider);
/* Numeric diagnostic values only. No owner pointer/data/nonce is exported. */
extern volatile uint32_t k70_nvram_ro_counters399[10];
#ifdef __cplusplus
/* Explicit controlled-bootstrap call only; DT opt-in must ALSO be exactly u32=1. */
int32_t k70_nvram_ro_create399(IOService *real_nvram_provider);
}
#endif
#endif