/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * macOS-only: real IOKit header first, then the own declarations. Incompatible redeclarations fail to compile. */
#include <IOKit/IOKitLib.h>
#include <stdio.h>
/* Force each identifier to exist in the REAL SDK before own declarations. */
typedef __typeof__(IOServiceMatching) sdk_decl_IOServiceMatching;
typedef __typeof__(IOServiceGetMatchingServices) sdk_decl_IOServiceGetMatchingServices;
typedef __typeof__(IOIteratorNext) sdk_decl_IOIteratorNext;
typedef __typeof__(IOIteratorIsValid) sdk_decl_IOIteratorIsValid;
typedef __typeof__(IOObjectRelease) sdk_decl_IOObjectRelease;
typedef __typeof__(IORegistryEntryFromPath) sdk_decl_IORegistryEntryFromPath;
typedef __typeof__(IOServiceGetBusyState) sdk_decl_IOServiceGetBusyState;
typedef __typeof__(IOObjectGetClass) sdk_decl_IOObjectGetClass;
#define io_object_t own_io_object_t
#define io_iterator_t own_io_iterator_t
#define io_registry_entry_t own_io_registry_entry_t
#define io_service_t own_io_service_t
#define io_string_t own_io_string_t
#define io_name_t own_io_name_t
#include "../src/iokitbusy_abi.h"
#undef io_name_t
#undef io_object_t
#undef io_iterator_t
#undef io_registry_entry_t
#undef io_service_t
#undef io_string_t
_Static_assert(sizeof(own_io_object_t) == sizeof(io_object_t), "io_object_t");
_Static_assert(sizeof(own_io_string_t) == sizeof(io_string_t), "io_string_t");
_Static_assert(sizeof(own_io_name_t) == sizeof(io_name_t), "io_name_t");
/* state bits are kernel-only constants (XNU iokit/IOKit/IOService.h:81-83: Inactive 1, Registered 2, Matched 4); no user header */
_Static_assert(IB_STATE_INACTIVE == 1u && IB_STATE_REGISTERED == 2u && IB_STATE_MATCHED == 4u, "state bits");
int main(void) { puts("PROTO PASS"); return 0; }
