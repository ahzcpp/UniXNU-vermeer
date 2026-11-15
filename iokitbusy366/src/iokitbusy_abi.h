/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Own declarations of the few IOKit.framework calls used by iokitbusy366 (the iPhoneOS SDK ships IOKit.tbd but no IOKit headers).
 * CI compiles tests/proto_check.c with the real macOS <IOKit/IOKitLib.h> first and then this header: any incompatible prototype
 * or constant is a compile error. */
#ifndef IOKITBUSY_ABI_H
#define IOKITBUSY_ABI_H
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <stdint.h>

typedef mach_port_t io_object_t;
typedef io_object_t io_iterator_t;
typedef io_object_t io_registry_entry_t;
typedef io_object_t io_service_t;
typedef char io_string_t[512];
typedef char io_name_t[128];

CFMutableDictionaryRef IOServiceMatching(const char *name);
kern_return_t IOServiceGetMatchingServices(mach_port_t mainPort, CFDictionaryRef matching, io_iterator_t *existing);
io_object_t IOIteratorNext(io_iterator_t iterator);
boolean_t IOIteratorIsValid(io_iterator_t iterator);
kern_return_t IOObjectRelease(io_object_t object);
io_registry_entry_t IORegistryEntryFromPath(mach_port_t mainPort, const io_string_t path);
kern_return_t IOServiceGetBusyState(io_service_t service, uint32_t *busy_state);
kern_return_t IOObjectGetClass(io_object_t object, io_name_t className); /* -w only: class name of busy entries */

/* IOService state bits (IOKit/IOKitKeys / IOService.h): only used to count, never printed per object */
#define IB_STATE_INACTIVE 0x00000001u
#define IB_STATE_REGISTERED 0x00000002u
#define IB_STATE_MATCHED 0x00000004u
#endif
