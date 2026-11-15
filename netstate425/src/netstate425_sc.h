/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * The only three SystemConfiguration calls netstate425 makes. Same approach as the own netpublish_sc.h (wifi-ci356 netdiag323):
 * own prototypes because the iPhoneOS SDK header marks SCDynamicStore API_UNAVAILABLE(ios); signatures as in Apple's public
 * SCDynamicStore.h / SCNetwork.h. CI should compile this header once against the real macOS header as a prototype check
 * (-DNS425_HAVE_SC_CONTEXT -include SystemConfiguration/SystemConfiguration.h). Whether each symbol is exported by the 20H392
 * framework is NOT verified here: dyld reports a missing one at launch (netpublish already imports the same three symbols).
 * Deliberately NOT declared: SCDynamicStoreSet..., SCDynamicStoreRemove..., SCDynamicStoreCopyKeyList/CopyMultiple, notification
 * APIs, SCPreferences..., SCNetworkInterface... - the tool cannot call them without editing this header. */
#ifndef NETSTATE425_SC_H
#define NETSTATE425_SC_H
#include <CoreFoundation/CoreFoundation.h>
#if defined(NETSTATE425_USE_SDK_HEADERS)
#include <SystemConfiguration/SystemConfiguration.h>
#else
typedef const struct __SCDynamicStore *SCDynamicStoreRef;
typedef void (*SCDynamicStoreCallBack)(SCDynamicStoreRef, CFArrayRef, void *);
#ifndef NS425_HAVE_SC_CONTEXT  /* the prototype check includes Apple's header, which defines it */
typedef struct {CFIndex version; void *info; const void *(*retain)(const void *); void (*release)(const void *); CFStringRef (*copyDescription)(const void *);} SCDynamicStoreContext;
#endif
extern SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef, CFStringRef, SCDynamicStoreCallBack, SCDynamicStoreContext *);
extern CFPropertyListRef SCDynamicStoreCopyValue(SCDynamicStoreRef, CFStringRef);
extern int SCError(void);
#endif
#endif
