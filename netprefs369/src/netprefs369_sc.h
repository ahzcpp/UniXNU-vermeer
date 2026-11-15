/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * System platform read-only calls exported by the target iOS framework.
 * The app-facing iPhoneOS headers mark them unavailable to third-party apps.
 * CI verifies every declaration against genuine macOS SystemConfiguration headers;
 * main separately verifies all target20H392 imports. No availability macro override.
 */
#ifndef NETPREFS369_SC_H
#define NETPREFS369_SC_H
#include <CoreFoundation/CoreFoundation.h>
typedef const struct __SCDynamicStore *SCDynamicStoreRef;
typedef const struct __SCPreferences *SCPreferencesRef;
typedef void (*SCDynamicStoreCallBack)(SCDynamicStoreRef, CFArrayRef, void *);
#ifndef NP369_SDK_CONTEXT_PRESENT
typedef struct {CFIndex version; void *info; const void *(*retain)(const void *); void (*release)(const void *); CFStringRef (*copyDescription)(const void *);} SCDynamicStoreContext;
#endif
extern SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef,CFStringRef,SCDynamicStoreCallBack,SCDynamicStoreContext *);
extern CFPropertyListRef SCDynamicStoreCopyValue(SCDynamicStoreRef,CFStringRef);
extern SCPreferencesRef SCPreferencesCreate(CFAllocatorRef,CFStringRef,CFStringRef);
extern CFPropertyListRef SCPreferencesGetValue(SCPreferencesRef,CFStringRef);
extern int SCError(void);
#define NP369_NO_KEY 1004
#endif
