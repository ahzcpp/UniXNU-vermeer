/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Own declarations of the public IOKit.framework calls used by iokitholder385 (the iPhoneOS SDK ships IOKit.tbd but no IOKit
 * headers). tests/proto_check.c compiles the real macOS <IOKit/IOKitLib.h> first and then this header, so any incompatible
 * prototype is a compile error. No private SPI: busy counts come from the public IOServiceGetBusyState only.
 * The same header is used by tests/mock_iokit.c (host simulation), so the probe logic is tested against these exact prototypes. */
#ifndef IOKITHOLDER_ABI_H
#define IOKITHOLDER_ABI_H
#include <mach/mach.h>
#include <stdint.h>

typedef mach_port_t io_object_t;
typedef io_object_t io_iterator_t;
typedef io_object_t io_registry_entry_t;
typedef io_object_t io_service_t;
typedef char io_string_t[512];
typedef char io_name_t[128];
typedef uint32_t ih_option_bits; /* IOOptionBits (UInt32) */

io_registry_entry_t IORegistryEntryFromPath(mach_port_t mainPort, const io_string_t path);
kern_return_t IORegistryEntryCreateIterator(io_registry_entry_t entry, const io_name_t plane, ih_option_bits options,
                                            io_iterator_t *iterator);
kern_return_t IORegistryEntryGetChildIterator(io_registry_entry_t entry, const io_name_t plane, io_iterator_t *iterator);
kern_return_t IORegistryEntryGetParentEntry(io_registry_entry_t entry, const io_name_t plane, io_registry_entry_t *parent);
io_object_t IOIteratorNext(io_iterator_t iterator);
boolean_t IOIteratorIsValid(io_iterator_t iterator);
boolean_t IOObjectIsEqualTo(io_object_t object, io_object_t anObject);
kern_return_t IOObjectRelease(io_object_t object);
kern_return_t IOServiceGetBusyState(io_service_t service, uint32_t *busy_state);
kern_return_t IOObjectGetClass(io_object_t object, io_name_t className);

#define IH_ITERATE_RECURSIVELY 0x00000001u /* kIORegistryIterateRecursively */
#endif
