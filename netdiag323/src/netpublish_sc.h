/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* The SystemConfiguration calls netpublish uses, declared here because the iPhoneOS SDK headers mark them API_UNAVAILABLE(ios)
 * although the iOS framework is what configd's own clients link. Signatures as in Apple's public SCDynamicStore.h /
 * SCNetworkConfiguration.h (identical prototypes on macOS, where this header is compiled against the real one for checking).
 * Key and property names are the literal strings of Apple's public SCSchemaDefinitions. Whether each symbol is exported by the
 * 20H392 framework is NOT verified here: dyld reports a missing one at launch. */
#ifndef NETPUBLISH_SC_H
#define NETPUBLISH_SC_H
#include <CoreFoundation/CoreFoundation.h>
#if defined(NETPUBLISH_USE_SDK_HEADERS)
#include <SystemConfiguration/SystemConfiguration.h>
#else
typedef const struct __SCDynamicStore *SCDynamicStoreRef;
typedef const struct __SCNetworkInterface *SCNetworkInterfaceRef;
typedef void (*SCDynamicStoreCallBack)(SCDynamicStoreRef, CFArrayRef, void *);
#ifndef NP_HAVE_SC_CONTEXT  /* the prototype check includes Apple's header, which defines it */
typedef struct {CFIndex version; void *info; const void *(*retain)(const void *); void (*release)(const void *); CFStringRef (*copyDescription)(const void *);} SCDynamicStoreContext;
#endif
extern SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef, CFStringRef, SCDynamicStoreCallBack, SCDynamicStoreContext *);
extern CFPropertyListRef SCDynamicStoreCopyValue(SCDynamicStoreRef, CFStringRef);
extern Boolean SCDynamicStoreSetMultiple(SCDynamicStoreRef, CFDictionaryRef, CFArrayRef, CFArrayRef);
extern int SCError(void);
extern const char *SCErrorString(int);
extern CFArrayRef SCNetworkInterfaceCopyAll(void);
extern CFStringRef SCNetworkInterfaceGetBSDName(SCNetworkInterfaceRef);
extern CFStringRef SCNetworkInterfaceGetInterfaceType(SCNetworkInterfaceRef);
#endif
#define NP_KEY_SERVICE_IPV4(id) CFSTR("State:/Network/Service/" id "/IPv4")
#define NP_KEY_SERVICE_DNS(id) CFSTR("State:/Network/Service/" id "/DNS")
#define NP_KEY_GLOBAL_IPV4 CFSTR("State:/Network/Global/IPv4")
#define NP_KEY_GLOBAL_DNS CFSTR("State:/Network/Global/DNS")
#define NP_PROP_ADDRESSES CFSTR("Addresses")
#define NP_PROP_SUBNET_MASKS CFSTR("SubnetMasks")
#define NP_PROP_ROUTER CFSTR("Router")
#define NP_PROP_INTERFACE_NAME CFSTR("InterfaceName")
#define NP_PROP_CONFIG_METHOD CFSTR("ConfigMethod")
#define NP_PROP_SERVER_ADDRESSES CFSTR("ServerAddresses")
#endif
