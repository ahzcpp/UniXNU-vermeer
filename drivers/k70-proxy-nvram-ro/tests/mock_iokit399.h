#pragma once
// HOST-ONLY fixture. Not native include headers; production source is extracted
// by deleting ONLY its IOKit includes. No native build uses this file.
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <new>
#include "NvramROExports399.h"
using IOReturn=int32_t;using IOByteCount=uint64_t;using UInt8=uint8_t;using IOOptionBits=uint32_t;
static constexpr IOReturn kIOReturnSuccess=0,kIOReturnNotReady=(int32_t)0xe00002d8,kIOReturnNotWritable=(int32_t)0xe00002cf,kIOReturnUnsupported=(int32_t)0xe00002c7,kIOReturnBadArgument=(int32_t)0xe00002c2,kIOReturnExclusiveAccess=(int32_t)0xe00002c5,kIOReturnBusy=(int32_t)0xe00002d5,kIOReturnNoMemory=(int32_t)0xe00002bd,kIOReturnNoResources=(int32_t)0xe00002be;
static int gLive=0,gAPIs=0,gGeneration=1,gBaseCalls=0,gIteratorCalls=0,gCopies=0,gProps=0;
static bool skip_reload,iter_overflow,fail_new,fail_init,fail_attach,fail_start,fail_copy,fail_prop,fail_iter,invalid_iter,change_generation,controller_on_second_iter;
static const int planeS=1,planeD=2;static const int*gIOServicePlane=&planeS,*gIODTPlane=&planeD;static constexpr unsigned kIORegistryIterateRecursively=1;
struct OSObject {int refs=1;OSObject(){gLive++;}virtual ~OSObject(){gLive--;}virtual void free(){};void retain(){assert(refs>0);refs++;}void release(){assert(refs>0);if(!--refs){free();delete this;}}};
template<class T>class OSPtr {T*p=nullptr;public:OSPtr()=default;explicit OSPtr(T*v):p(v){};OSPtr(const OSPtr&v):p(v.p){if(p)p->retain();}OSPtr(OSPtr&&v):p(v.p){v.p=nullptr;}~OSPtr(){if(p)p->release();}T*get()const{return p;}T*operator->()const{return p;}explicit operator bool()const{return p!=nullptr;}};
struct OSBoolean:OSObject{};static OSBoolean*kOSBooleanTrue,*kOSBooleanFalse;
struct OSData:OSObject {std::vector<uint8_t>b;explicit OSData(std::vector<uint8_t>v):b(std::move(v)){};static OSPtr<OSData>withBytes(const void*p,unsigned n){gAPIs++;gCopies++;if(fail_copy)return {};auto*d=new OSData(std::vector<uint8_t>((const uint8_t*)p,(const uint8_t*)p+n));return OSPtr<OSData>(d);}unsigned getLength()const{return b.size();}const void*getBytesNoCopy()const{return b.data();}};
#define OSDynamicCast(T,p) dynamic_cast<T*>(p)
#define OSDeclareDefaultStructors(T) public:T()=default;~T()override=default;private:
#define OSDefineMetaClassAndStructors(T,B)
class IOService;struct IORegistryEntry;static IORegistryEntry*gChosen;static IOService*gRoot,*gResources,*gPlatform;static std::vector<IORegistryEntry*>gEntries;
struct IORegistryEntry:OSObject {
 std::map<std::string,OSObject*>props;bool inplane=true;
 void free()override{for(auto&kv:props)kv.second->release();props.clear();OSObject::free();}
 virtual bool inPlane(const int*)const{gAPIs++;return inplane;}
 virtual bool setProperty(const char*k,OSObject*v){gAPIs++;gProps++;if(fail_prop)return false;auto it=props.find(k);if(it!=props.end())it->second->release();v->retain();props[k]=v;return true;}
 bool setProperty(const char*k,uint32_t,unsigned){return setProperty(k,kOSBooleanTrue);}
 OSPtr<OSObject>copyProperty(const char*k){gAPIs++;auto it=props.find(k);if(it==props.end())return {};it->second->retain();return OSPtr<OSObject>(it->second);}
 static OSPtr<IORegistryEntry>fromPath(const char*p,const int*){gAPIs++;assert(!strcmp(p,"/chosen"));if(!gChosen)return {};gChosen->retain();return OSPtr<IORegistryEntry>(gChosen);}
 static int getGenerationCount(){gAPIs++;return gGeneration;}
};
class IOService:public IORegistryEntry { public:
 static void*operator new(size_t n)noexcept{if(fail_new)return nullptr;return ::operator new(n,std::nothrow);}static void operator delete(void*p)noexcept{::operator delete(p);}
 IOService*provider=nullptr;virtual bool init(){gAPIs++;return !fail_init;}
 virtual bool attach(IOService*p){gAPIs++;if(fail_attach)return false;provider=p;p->retain();gEntries.push_back(this);gGeneration++;return true;}
 virtual void detach(IOService*p){gAPIs++;assert(p==provider);provider=nullptr;p->release();auto it=std::find(gEntries.begin(),gEntries.end(),this);assert(it!=gEntries.end());gEntries.erase(it);gGeneration++;}
 virtual bool start(IOService*){gAPIs++;return !fail_start;}virtual void stop(IOService*){gAPIs++;}virtual void registerService(IOOptionBits=0){gAPIs++;}
 IOService*getProvider(){gAPIs++;return provider;}static IOService*getServiceRoot(){gAPIs++;return gRoot;}static IOService*getResourceService(){gAPIs++;return gResources;}static IOService*getPlatform(){gAPIs++;return gPlatform;}
};
struct IOPlatformExpertDevice:IOService{};struct ArmDTPlatform:IOService{};struct IONVRAMController;
struct IODTNVRAM:IOService{IONVRAMController*controller=nullptr;uint32_t cacheMarker=0x12345678;unsigned reloads=0;void registerNVRAMController(IONVRAMController*);};
struct IONVRAMController:IOService {
 virtual void sync(){}virtual IOReturn select(uint32_t){return kIOReturnUnsupported;}virtual IOReturn eraseBank(){return kIOReturnUnsupported;}
 virtual IOReturn read(IOByteCount,UInt8*,IOByteCount)=0;virtual IOReturn write(IOByteCount,UInt8*,IOByteCount)=0;
 void registerService(IOOptionBits options=0)override{assert(!options);assert(k70_nvram_ro_active399());assert(k70_nvram_ro_for_provider399(provider));assert(!k70_nvram_ro_for_provider399(gPlatform));uint8_t header[32]={};assert(select(0)==0);assert(read(0,header,sizeof header)==0);assert(header[0]==0x70);dynamic_cast<IODTNVRAM*>(provider)->registerNVRAMController(this);gBaseCalls++;gResources->setProperty("IONVRAM",kOSBooleanTrue);}
};
struct OtherController:IONVRAMController {IOReturn read(IOByteCount,UInt8*,IOByteCount)override{return 0;}IOReturn write(IOByteCount,UInt8*,IOByteCount)override{return 0;}};
struct IORegistryIterator:OSObject {std::vector<IORegistryEntry*>entries;size_t pos=0;IORegistryIterator(){entries=gEntries;if(iter_overflow&&!entries.empty())entries.resize(4097,entries.front());for(auto*e:entries)e->retain();}void free()override{for(auto*e:entries)e->release();OSObject::free();}
 static OSPtr<IORegistryIterator>iterateOver(IORegistryEntry*,const int*,unsigned){gAPIs++;gIteratorCalls++;if(fail_iter)return {};if(controller_on_second_iter&&gIteratorCalls==2){auto*c=new OtherController;assert(c->attach(gRoot));/* external owner keeps initial ref through the failure scenario */}return OSPtr<IORegistryIterator>(new IORegistryIterator);}
 IORegistryEntry*getNextObject(){gAPIs++;if(change_generation&&pos==0)gGeneration++;return pos<entries.size()?entries[pos++]:nullptr;}bool isValid(){gAPIs++;return !invalid_iter;}
};

inline void IODTNVRAM::registerNVRAMController(IONVRAMController*c){gAPIs++;if(!controller)controller=c;reloads++;if(skip_reload)return;uint8_t header[32];controller->select(0);controller->read(0,header,32);}
