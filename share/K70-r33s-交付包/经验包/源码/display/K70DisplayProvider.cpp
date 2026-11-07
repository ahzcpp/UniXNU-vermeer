// SPDX-License-Identifier: BSD-2-Clause-Patent
// Display-only lifecycle adapted from UniXNU-sheng LegacyDisplayProbe.cpp and
// Trogdor-ipadOS CoachzDisplay.cpp (exact references in reference/pins.json).
// Firmware framebuffer only. No GPU, DPU, touch, clock, key-store or power MMIO.
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOUserClient.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <mach/kmod.h>
#include <kern/task.h>
#include <sys/proc.h>
#include <sys/errno.h>
extern "C" kmod_info_t g_kernel_kmod_info;
extern "C" void k70_legacy_static_init(void) __asm__("__GLOBAL__sub_I_NativeLegacyDisplay.cpp");
extern "C" IOService *native_legacy_display_allocate(void);
extern "C" kern_return_t OSKextLoadKextWithIdentifier(const char *);
extern "C" int boot_progress_prepare_scanout(void) __attribute__((weak_import));
static IOService *activeDisplay;
extern "C" {
volatile uint32_t k70_display_stage,k70_display_registrations,k70_display_clients;
volatile uint32_t native_legacy_surfaces[14],native_legacy_direct[14];
volatile uint64_t native_legacy_early_retires,native_legacy_early_failures,native_legacy_swap_timeouts;
volatile uint64_t k70_display_frames,k70_display_rejections,k70_display_transfers,k70_display_bytes,k70_display_retirements;
volatile int k70_display_last_transfer=kIOReturnNotReady;
bool native_legacy_prepare_scanout(void) {
    // Current minimal observer has no surrender API. Registering a display does
    // not authorize it to compete with the diagnostic renderer for scanout.
    return activeDisplay && boot_progress_prepare_scanout && boot_progress_prepare_scanout()!=0;
}
void native_legacy_swap_stage(unsigned) {}
void native_legacy_pm_observed(unsigned stage,IOReturn status,unsigned long ordinal) {
    kprintf("K70 display: logical PM %u status %x ordinal %lu\n",stage,status,ordinal);
}
void native_legacy_client_enter(task_t task,uint32_t type) {
    uint32_t n=++k70_display_clients;
    if(n<=12)kprintf("K70 display: open %u pid %d type %u\n",n,task?proc_pid((proc_t)get_bsdtask_info(task)):0,type);
}
void native_legacy_client_observed(IOReturn status,IOUserClient *) {
    if(k70_display_clients<=12)kprintf("K70 display: open result %x\n",status);
}
void native_legacy_registration_observed(IOService *) {
    ++k70_display_registrations;kprintf("K70 display: registered %u\n",k70_display_registrations);
}
void native_legacy_vsync_observed(unsigned stage,unsigned hz) {
    if(stage==4 || stage==5)kprintf("K70 display: software refresh stage %u Hz %u\n",stage,hz);
}
bool native_legacy_gop_mapping_observed(IOService *owner,IOMemoryMap *map,uint32_t width,uint32_t height) {
    PE_Video video={};
    bool valid=owner && !PE_current_console(&video) && map && map->getVirtualAddress() &&
        map->getPhysicalAddress()==video.v_baseAddr && map->getLength()==video.v_rowBytes*video.v_height &&
        width==video.v_width && height==video.v_height;
    kprintf("K70 display: firmware mapping %u width %u height %u stride %lu\n",valid,width,height,video.v_rowBytes);
    return valid;
}
void native_legacy_frame_observed(unsigned,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t) {++k70_display_frames;}
void native_legacy_frame_colors_observed(const uint32_t *,const uint64_t *,uint64_t) {}
void native_legacy_swap_validation_observed(bool accepted,unsigned reason,const void *,const void *) {
    if(!accepted && ++k70_display_rejections<=12)kprintf("K70 display: rejected swap reason %u\n",reason);
}
void native_legacy_surface_rejected(uint64_t,uint64_t,uint32_t,uint32_t,uint64_t,uint64_t,uint64_t) {}
void native_legacy_copy_timing_observed(uint64_t,uint64_t) {}
void native_legacy_transfer_observed(IOReturn status,const void *,uint64_t bytes) {
    ++k70_display_transfers;k70_display_last_transfer=status;
    if(status==kIOReturnSuccess)k70_display_bytes+=bytes;
    if(k70_display_transfers<=12)kprintf("K70 display: transfer %x bytes %llu\n",status,bytes);
}
void native_legacy_retirement_observed(IOService *) {++k70_display_retirements;}
bool native_legacy_gpu_offer(void *,IOMemoryDescriptor *,uint64_t,uint32_t,uint32_t,uint32_t) {return false;}
void native_legacy_gpu_fallback_observed(int) {}
__attribute__((noreturn)) void native_legacy_backend_guard(unsigned slot,void *,void *caller) {
    panic("K70 display unqualified backend slot %u caller %p stage %u registrations %u",slot,caller,k70_display_stage,k70_display_registrations);
}
static bool word(IORegistryEntry *entry,const char *key,uint32_t value) {
    if(OSObject *old=entry->getProperty(key)) {
        OSData *data=OSDynamicCast(OSData,old);uint32_t actual=0;
        if(!data || data->getLength()!=4)return false;
        memcpy(&actual,data->getBytesNoCopy(),4);return actual==value;
    }
    OSData *data=OSData::withBytes(&value,4);
    bool ok=data && entry->setProperty(key,data);OSSafeReleaseNULL(data);return ok;
}
int k70_display_start(void) {
    unsigned enabled=0;
    if(!PE_parse_boot_argn("k70-display",&enabled,sizeof(enabled)) || enabled!=1)return 0;
    // This candidate deliberately has no hardware-specific present backend.
    const char *const hardwareOptions[]={"sheng-dpu-scanout","sheng-fb-gpu","sheng-gpu-power","sheng-gpu-metal"};
    for(const char *key:hardwareOptions) {
        unsigned value=0;if(PE_parse_boot_argn(key,&value,sizeof(value)) && value)return ENOTSUP;
    }
    if(k70_display_stage)return k70_display_stage==10?0:EIO;
    k70_display_stage=1;
    kern_return_t surface=OSKextLoadKextWithIdentifier("com.apple.iokit.IOSurface");
    kern_return_t family=OSKextLoadKextWithIdentifier("com.apple.iokit.IOMobileGraphicsFamily");
    kprintf("K70 display: IOSurface %x MobileGraphics %x\n",surface,family);
    if(surface || family)return EIO;
    OSDictionary *match=IOService::nameMatching("IOCoreSurfaceRoot");
    IOService *root=match?IOService::waitForMatchingService(match,5000000000ULL):nullptr;
    bool ready=root!=nullptr;OSSafeReleaseNULL(root);OSSafeReleaseNULL(match);
    if(!ready)return ENODEV;
    k70_display_stage=2;
    void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if(!handle)return EIO;
    k70_legacy_static_init();OSReturn meta=OSMetaClass::postModLoad(handle);
    if(meta)return EIO;
    PE_Video video={};
    if(PE_current_console(&video) || !video.v_baseAddr || !video.v_width || !video.v_height || video.v_depth!=32 ||
       video.v_rotate || video.v_display!=1 || video.v_width>16384 || video.v_height>16384 ||
       video.v_rowBytes<video.v_width*4 || video.v_rowBytes>UINT64_MAX/video.v_height)return EINVAL;
    IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
    bool scale=chosen && word(chosen,"display-scale",2) && word(chosen,"display-rotation",0);
    OSSafeReleaseNULL(chosen);if(!scale)return EINVAL;
    IOService *display=native_legacy_display_allocate();
    if(!display || !display->metaCast("IOMobileFramebufferLegacy") || !display->metaCast("IOMobileFramebuffer"))return EIO;
    k70_display_stage=3;
    if(!display->init())return EIO;
    IOPlatformDevice *nub=new IOPlatformDevice;
    IOService *parent=IOService::getResourceService();
    IODeviceMemory *memory=IODeviceMemory::withRange(video.v_baseAddr,video.v_rowBytes*video.v_height);
    OSArray *ranges=OSArray::withCapacity(1);
    if(!nub || !parent || !memory || !ranges || !nub->init())return ENOMEM;
    ranges->setObject(memory);nub->setDeviceMemory(ranges);nub->setName("K70FirmwareFramebuffer");
    OSSafeReleaseNULL(ranges);OSSafeReleaseNULL(memory);
    if(!nub->attach(parent) || !nub->start(parent))return EIO;
    k70_display_stage=4;
    IORegistryEntry *armio=IORegistryEntry::fromPath("/arm-io",gIODTPlane);
    const char type[]="display-subsystem";OSData *data=OSData::withBytes(type,sizeof(type));
    bool attached=armio && data && nub->setProperty("device_type",data);
    nub->setName("k70-gop-display",gIODTPlane);
    attached=attached && nub->attachToParent(armio,gIODTPlane);
    OSSafeReleaseNULL(data);OSSafeReleaseNULL(armio);
    if(!attached)return ENODEV;
    SInt32 score=0;
    if(display->probe(nub,&score)!=display || !display->attach(nub))return EIO;
    k70_display_stage=5;activeDisplay=display;
    if(!display->start(nub))return EIO;
    k70_display_stage=10;
    kprintf("K70 display: Legacy provider active %lux%lu scale2 registered %u scanout-yield %u\n",
        video.v_width,video.v_height,k70_display_registrations,boot_progress_prepare_scanout!=nullptr);
    return k70_display_registrations?0:EIO;
}
}
