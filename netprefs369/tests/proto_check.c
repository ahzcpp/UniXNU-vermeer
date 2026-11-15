#include <SystemConfiguration/SystemConfiguration.h>
#include <stddef.h>
typedef __typeof__(SCDynamicStoreCreate) sdk_decl_SCDynamicStoreCreate;
typedef __typeof__(SCDynamicStoreCopyValue) sdk_decl_SCDynamicStoreCopyValue;
typedef __typeof__(SCPreferencesCreate) sdk_decl_SCPreferencesCreate;
typedef __typeof__(SCPreferencesGetValue) sdk_decl_SCPreferencesGetValue;
typedef __typeof__(SCError) sdk_decl_SCError;
#define NP369_SDK_CONTEXT_PRESENT
#include "../src/netprefs369_sc.h"
_Static_assert(NP369_NO_KEY==kSCStatusNoKey,"real SDK NoKey");
_Static_assert(sizeof(SCDynamicStoreContext)==40 && offsetof(SCDynamicStoreContext,copyDescription)==32,"real arm64 SDK context");
int main(void){return 0;}
