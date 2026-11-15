/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Read-only VM observations. Never changes VM/Jetsam or writes any sysctl. */
#ifndef NETSNAP_MEMORY351_H
#define NETSNAP_MEMORY351_H
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
typedef void (*ns351_mem_emit_fn)(int,const char *,...) __attribute__((format(printf,2,3)));
static inline const char *ns351_mem_u64(int valid,uint64_t value,char text[32]) {
    if(!valid)return "unknown";
    snprintf(text,32,"%llu",(unsigned long long)value);return text;
}
static inline void ns351_memory_snapshot(int index,ns351_mem_emit_fn log) {
    /* Keep the original finite console budget. All samples still go to stdout. */
    const int mirror=index<2;
    uint64_t memsize=0;size_t memlen=sizeof memsize;errno=0;
    int memrc=sysctlbyname("hw.memsize",&memsize,&memlen,NULL,0);const int memerr=errno;
    uint32_t pagesize=0;size_t pagelen=sizeof pagesize;errno=0;
    int pagerc=sysctlbyname("hw.pagesize",&pagesize,&pagelen,NULL,0);const int pageerr=errno;
    int memok=!memrc && memlen==sizeof memsize,pageok=!pagerc && pagelen==sizeof pagesize;
    char mt[32],pt[32];
    log(mirror,"memory_hw n=%d memsize_known=%d memsize_rc=%d memsize_errno=%d memsize_len=%zu memsize_bytes=%s pagesize_known=%d pagesize_rc=%d pagesize_errno=%d pagesize_len=%zu pagesize_bytes=%s",
        index,memok,memrc,memerr,memlen,ns351_mem_u64(memok,memsize,mt),pageok,pagerc,pageerr,pagelen,ns351_mem_u64(pageok,pagesize,pt));
    host_t host=mach_host_self();
    const mach_msg_type_number_t requested=HOST_VM_INFO64_COUNT;
    if(!MACH_PORT_VALID(host)) {
        log(mirror,"memory_vm n=%d quality=unknown host_valid=0 stat_called=0 kr=not_called requested_count=%u returned_count=unknown dealloc_called=0 dealloc_kr=not_called free_pages=unknown active_pages=unknown inactive_pages=unknown wired_pages=unknown purgeable_pages=unknown compressed_pages=unknown",index,(unsigned)requested);
        log(mirror,"memory_io n=%d quality=unknown pageins=unknown pageouts=unknown counters=cumulative",index);
        return;
    }
    vm_statistics64_data_t vm;memset(&vm,0,sizeof vm);
    mach_msg_type_number_t count=requested;
    kern_return_t kr=host_statistics64(host,HOST_VM_INFO64,(host_info64_t)&vm,&count);
    /* Release exactly this acquired host right, even after a failed/short read.
     * mach_task_self() is not acquired by this observer and is not deallocated. */
    kern_return_t released=mach_port_deallocate(mach_task_self(),host);
    int count_ok=kr==KERN_SUCCESS && count<=requested;
    size_t bytes=count_ok ? (size_t)count*sizeof(integer_t) : 0;
    int base_ok=count_ok && bytes>=offsetof(vm_statistics64_data_t,purgeable_count)+sizeof vm.purgeable_count;
    int compressed_ok=count_ok && bytes>=offsetof(vm_statistics64_data_t,compressor_page_count)+sizeof vm.compressor_page_count;
    const char *quality=base_ok ? (compressed_ok ? "complete" : "partial") : "unknown";
    char ft[32],at[32],it[32],wt[32],qt[32],ct[32],intxt[32],outtxt[32];
    log(mirror,"memory_vm n=%d quality=%s host_valid=1 stat_called=1 kr=%d requested_count=%u returned_count=%u base_known=%d compressed_known=%d dealloc_called=1 dealloc_kr=%d free_pages=%s active_pages=%s inactive_pages=%s wired_pages=%s purgeable_pages=%s compressed_pages=%s",
        index,quality,(int)kr,(unsigned)requested,(unsigned)count,base_ok,compressed_ok,(int)released,
        ns351_mem_u64(base_ok,vm.free_count,ft),ns351_mem_u64(base_ok,vm.active_count,at),ns351_mem_u64(base_ok,vm.inactive_count,it),
        ns351_mem_u64(base_ok,vm.wire_count,wt),ns351_mem_u64(base_ok,vm.purgeable_count,qt),ns351_mem_u64(compressed_ok,vm.compressor_page_count,ct));
    log(mirror,"memory_io n=%d quality=%s pageins=%s pageouts=%s counters=cumulative",index,base_ok ? "complete" : "unknown",
        ns351_mem_u64(base_ok,vm.pageins,intxt),ns351_mem_u64(base_ok,vm.pageouts,outtxt));
}
#endif
