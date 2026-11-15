/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * macOS-only (Main's CI): real IOKit header first, then the own declarations. Incompatible redeclarations fail to compile. */
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#define io_object_t own_io_object_t
#define io_iterator_t own_io_iterator_t
#define io_registry_entry_t own_io_registry_entry_t
#define io_service_t own_io_service_t
#define io_string_t own_io_string_t
#define io_name_t own_io_name_t
#include "../src/iokitholder_abi.h"
#undef io_name_t
#undef io_object_t
#undef io_iterator_t
#undef io_registry_entry_t
#undef io_service_t
#undef io_string_t
_Static_assert(sizeof(own_io_object_t) == sizeof(io_object_t), "io_object_t");
_Static_assert(sizeof(own_io_string_t) == sizeof(io_string_t), "io_string_t");
_Static_assert(sizeof(own_io_name_t) == sizeof(io_name_t), "io_name_t");
_Static_assert(sizeof(ih_option_bits) == sizeof(IOOptionBits), "IOOptionBits");
_Static_assert(IH_ITERATE_RECURSIVELY == kIORegistryIterateRecursively, "kIORegistryIterateRecursively");
int main(void) { puts("PROTO PASS"); return 0; }
