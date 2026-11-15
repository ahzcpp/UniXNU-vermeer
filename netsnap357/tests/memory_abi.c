/* SDK compile-only check; independent actual-XNU checks recorded separately. */
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <stddef.h>
_Static_assert(offsetof(vm_statistics64_data_t,free_count)==0,"free");
_Static_assert(offsetof(vm_statistics64_data_t,active_count)==4,"active");
_Static_assert(offsetof(vm_statistics64_data_t,inactive_count)==8,"inactive");
_Static_assert(offsetof(vm_statistics64_data_t,wire_count)==12,"wired");
_Static_assert(offsetof(vm_statistics64_data_t,pageins)==32,"pageins");
_Static_assert(offsetof(vm_statistics64_data_t,pageouts)==40,"pageouts");
_Static_assert(offsetof(vm_statistics64_data_t,purgeable_count)==88,"purgeable");
_Static_assert(offsetof(vm_statistics64_data_t,compressor_page_count)==128,"compressor");
_Static_assert(_Alignof(vm_statistics64_data_t)==8,"VM statistics LP64 alignment");
_Static_assert(HOST_VM_INFO64==4,"flavor");
