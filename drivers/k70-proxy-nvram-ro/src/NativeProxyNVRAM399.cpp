/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * OFFLINE DEVELOPMENT PROTOTYPE. No automatic instance or bootstrap hook.
 * Main must call factory only after PE dtNVRAM assignment, before consumers,
 * outside NVRAM locks, in a controlled first-controller enrollment phase.
 * Metadata: readonly=true; persistent=false; persistent_write_success=false.
 */
#include <IOKit/IONVRAM.h>
#include <IOKit/nvram/IONVRAMController.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSBoolean.h>
#include "ProxyBank399.h"
#include "NvramROExports399.h"

using namespace k70ro399;
static Activation gActivation399={};
static uint32_t gFactoryBusy399;
extern "C" { volatile uint32_t k70_nvram_ro_counters399[10]={}; }
extern "C" bool k70_nvram_ro_active399(void){return is_active(&gActivation399);}
extern "C" bool k70_nvram_ro_for_provider399(const void*p){return for_provider(&gActivation399,p);}
static void note399(unsigned i){__atomic_add_fetch(&k70_nvram_ro_counters399[i],1u,__ATOMIC_RELAXED);}
static IOReturn result399(IOReturn r){__atomic_store_n(&k70_nvram_ro_counters399[8],(uint32_t)r,__ATOMIC_RELAXED);return r;}
static IOReturn native399(Status s){switch(s){case ok:return kIOReturnSuccess;case not_ready:return kIOReturnNotReady;case not_writable:return kIOReturnNotWritable;case unsupported:return kIOReturnUnsupported;default:return kIOReturnBadArgument;}}
static IOReturn chosen_u32_399(IORegistryEntry*c,const char*key,uint32_t*out){
 auto prop=c->copyProperty(key);OSData*d=OSDynamicCast(OSData,prop.get());
 if(!d||d->getLength()!=4||!d->getBytesNoCopy())return kIOReturnBadArgument;
 *out=le32((const uint8_t*)d->getBytesNoCopy());return kIOReturnSuccess;
}
static IOReturn optin399(IORegistryEntry*c){
 auto prop=c->copyProperty("k70-nvram-proxy-ro");if(!prop)return kIOReturnNotReady;
 OSData*d=OSDynamicCast(OSData,prop.get());if(!d||d->getLength()!=4||!d->getBytesNoCopy())return kIOReturnBadArgument;
 return le32((const uint8_t*)d->getBytesNoCopy())==1?kIOReturnSuccess:kIOReturnNotReady;
}
/* Includes attached but not matched controllers. All iterator entries are BORROWED;
 * native IORegistryIterator owns them. Never release getNextObject() results. */
static IOReturn exclusive399(const IORegistryEntry*self){
 IOService*resources=IOService::getResourceService();IOService*root=IOService::getServiceRoot();
 if(!resources||!root)return kIOReturnNotReady;
 auto published=resources->copyProperty("IONVRAM");if(published)return kIOReturnExclusiveAccess;
 const int32_t generation=IORegistryEntry::getGenerationCount();
 auto iter=IORegistryIterator::iterateOver(root,gIOServicePlane,kIORegistryIterateRecursively);
 if(!iter)return kIOReturnNotReady;
 unsigned seen=0;IORegistryEntry*entry=nullptr;
 while(seen<4096&&(entry=iter->getNextObject())){
  seen++;if(entry!=self&&OSDynamicCast(IONVRAMController,entry))return kIOReturnExclusiveAccess;
 }
 if(seen==4096&&iter->getNextObject())return kIOReturnBusy;
 if(!iter->isValid()||generation!=IORegistryEntry::getGenerationCount())return kIOReturnBusy;
 return kIOReturnSuccess;
}
/* Real IODTNVRAM is created by IOPlatformExpertDevice::createNVRAM and
 * attached to the unique IOService root nub, NOT to gIOPlatform/ArmDTPlatform. */
static bool real_provider399(IOService*provider){
 IOService*root=IOService::getServiceRoot();
 return provider&&root&&OSDynamicCast(IOPlatformExpertDevice,root)&&OSDynamicCast(IODTNVRAM,provider)&&provider->inPlane(gIOServicePlane)&&provider->getProvider()==root;
}
class NativeProxyNVRAM399:public IONVRAMController {
 OSDeclareDefaultStructors(NativeProxyNVRAM399);
 friend int32_t k70_nvram_ro_create399(IOService*);
 OSData*_snapshot=nullptr;IOService*_bound=nullptr;Bank _bank={};
 bool _started=false,_armed=false,_registered=false,_prebindAttempted=false;
 uint32_t _headerReads399=0;IOReturn _registerResult=kIOReturnNotReady;
 IOReturn capture(IOService*,IORegistryEntry*);
 bool parent_valid() const;
public:
 bool start(IOService*) override;
 void free() override;
 void registerService(IOOptionBits options=0) override;
 IOReturn select(uint32_t bank) override;
 IOReturn eraseBank(void) override;
 IOReturn read(IOByteCount,UInt8*,IOByteCount) override;
 IOReturn write(IOByteCount,UInt8*,IOByteCount) override;
 void sync(void) override;
};
#define super IONVRAMController
OSDefineMetaClassAndStructors(NativeProxyNVRAM399,IONVRAMController);
/* Owns one permanent reference after activation: CHRP stores a raw controller
 * pointer. No deactivation/unload API is offered; lifetime is the kernel boot. */
static NativeProxyNVRAM399*gInstalled399;
bool NativeProxyNVRAM399::parent_valid() const {
 return real_provider399(_bound);
}
IOReturn NativeProxyNVRAM399::capture(IOService*provider,IORegistryEntry*chosen){
 Geometry g={};IOReturn r;
 if((r=chosen_u32_399(chosen,"nvram-bank-size",&g.size))!=kIOReturnSuccess)return r;
 if((r=chosen_u32_399(chosen,"nvram-bank-count",&g.count))!=kIOReturnSuccess)return r;
 if((r=chosen_u32_399(chosen,"nvram-current-bank",&g.current))!=kIOReturnSuccess)return r;
 auto prop=chosen->copyProperty("nvram-proxy-data");OSData*input=OSDynamicCast(OSData,prop.get());
 if(!input||!input->getBytesNoCopy())return kIOReturnNotReady;
 if(g.count!=1||g.current!=0)return kIOReturnUnsupported;
 if(input->getLength()!=g.size||g.size<48||g.size>(1u<<20)||(g.size&15))return kIOReturnBadArgument;
 auto copy=OSData::withBytes(input->getBytesNoCopy(),input->getLength());if(!copy)return kIOReturnNoMemory;
 Bank checked={};Status v=validate((const uint8_t*)copy->getBytesNoCopy(),copy->getLength(),g,&checked);
 if(v!=ok){note399(2);return native399(v);}
 /* Private deep copy only. NEVER expose this OSData as a registry property. */
 _snapshot=copy.get();_snapshot->retain();_bank=checked;_bound=provider;_bound->retain();
 if(!setProperty("readonly",kOSBooleanTrue)||!setProperty("persistent",kOSBooleanFalse)||
    !setProperty("ReadOnly",kOSBooleanTrue)||!setProperty("NonPersistent",kOSBooleanTrue)||
    !setProperty("PersistentWriteSuccess",kOSBooleanFalse)||!setProperty("BankSize",g.size,32)||!setProperty("BankCount",g.count,32))return kIOReturnNoMemory;
 note399(9);return kIOReturnSuccess;
}
bool NativeProxyNVRAM399::start(IOService*provider){
 if(!_bank.ready||provider!=_bound||!parent_valid())return false;
 if(!super::start(provider))return false;_started=true;return true;
}
void NativeProxyNVRAM399::free(){
 /* Only instances released BEFORE prebind can reach here through the factory. */
 _bank=Bank{};if(_snapshot){_snapshot->release();_snapshot=nullptr;}
 if(_bound){_bound->release();_bound=nullptr;}super::free();
}
IOReturn NativeProxyNVRAM399::read(IOByteCount off,UInt8*buffer,IOByteCount length){
 note399(3);IOReturn r=native399(k70ro399::read(_bank,off,buffer,length));
 // CHRP findCurrentBank's actual apple_nvram_header is exactly 32 bytes.
 // Count only this instance's successfully filled header, no provider callback.
 if(r==kIOReturnSuccess&&off==0&&length==32)__atomic_add_fetch(&_headerReads399,1u,__ATOMIC_RELAXED);
 return result399(r);
}
IOReturn NativeProxyNVRAM399::select(uint32_t bank){return result399(native399(k70ro399::select(_bank,bank)));}
IOReturn NativeProxyNVRAM399::write(IOByteCount,UInt8*,IOByteCount){note399(4);return result399(native399(reject_write(_bank)));}
IOReturn NativeProxyNVRAM399::eraseBank(void){note399(5);return result399(native399(reject_write(_bank)));}
void NativeProxyNVRAM399::sync(void){note399(6);/* void API: documented NO-OP, NEVER a durability acknowledgement. */}
void NativeProxyNVRAM399::registerService(IOOptionBits options){
 if(!_armed||_registered||options){_registerResult=kIOReturnNotReady;return;}_armed=false;
 IOReturn r=exclusive399(this);Admission a={true,parent_valid(),_bank.ready,_started,r==kIOReturnSuccess};
 if(!may_activate(a)){_registerResult=r==kIOReturnSuccess?kIOReturnNotReady:r;return;}
 // setController stores a raw pointer BEFORE reload; pin BEFORE attempting it.
 // Keep this reference EVEN on any later failure (no unbind contract exists).
 retain();_prebindAttempted=true;
 const uint32_t before=__atomic_load_n(&_headerReads399,__ATOMIC_RELAXED);
 static_cast<IODTNVRAM*>(_bound)->IODTNVRAM::registerNVRAMController(this);
 const uint32_t after=__atomic_load_n(&_headerReads399,__ATOMIC_RELAXED);
 // Native CHRP first-controller-wins + unconditional reload: a hidden existing
 // controller reads its own header instead. Intent alone is NEVER admission.
 if(after==before){_registerResult=kIOReturnExclusiveAccess;return;}
 if(!activate(&gActivation399,_bound)){_registerResult=kIOReturnExclusiveAccess;return;}
 gInstalled399=this;_registered=true;_registerResult=kIOReturnSuccess;note399(7);
 /* Last irreversible step; ordinary base is the ONLY resource publication path.
  * Main must already pair provider-bound setter/remove/reset/sync/serialize guards. */
 super::registerService(0);
}
extern "C" int32_t k70_nvram_ro_create399(IOService*provider){
 note399(0);uint32_t expected=0;
 if(!__atomic_compare_exchange_n(&gFactoryBusy399,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return result399(kIOReturnBusy);
 struct Leave{~Leave(){__atomic_store_n(&gFactoryBusy399,0u,__ATOMIC_RELEASE);}}leave;
 if(is_active(&gActivation399))return result399(kIOReturnExclusiveAccess);
 if(!real_provider399(provider))return result399(kIOReturnBadArgument);
 auto chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);if(!chosen)return result399(kIOReturnNotReady);
 IOReturn r=optin399(chosen.get());if(r!=kIOReturnSuccess)return result399(r);
 r=exclusive399(nullptr);if(r!=kIOReturnSuccess){note399(1);return result399(r);}
 auto*c=new NativeProxyNVRAM399;if(!c)return result399(kIOReturnNoMemory);
 if(!c->init()){c->release();return result399(kIOReturnNoMemory);}
 r=c->capture(provider,chosen.get());if(r!=kIOReturnSuccess){c->release();return result399(r);}
 if(!c->attach(provider)){c->release();return result399(kIOReturnNoResources);}
 if(!c->start(provider)){c->detach(provider);c->release();return result399(kIOReturnNotReady);}
 c->_armed=true;c->registerService(0);r=c->_registerResult;
 if(!c->_registered&&!c->_prebindAttempted){c->stop(provider);c->detach(provider);}
 c->release(); /* prebind attempt retains forever; earlier failure frees all own holds */
 return result399(r);
}
#undef super