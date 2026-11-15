#include "mock_iokit399.h"
#include "fixture399.h"
#include "actual-native-glue.inc"
#include <cstdio>
static unsigned checks;
#define C(x) do{assert(x);checks++;}while(0)
static void prop32(IORegistryEntry*p,const char*k,uint32_t v){uint8_t b[4];put32(b,v);auto d=OSData::withBytes(b,4);assert(p->setProperty(k,d.get()));}
int main(int argc,char**argv){assert(argc==2);std::string test=argv[1];
 kOSBooleanTrue=new OSBoolean;kOSBooleanFalse=new OSBoolean;gRoot=new IOPlatformExpertDevice;gPlatform=new ArmDTPlatform;gResources=new IOService;gChosen=new IORegistryEntry;
 auto*nv=new IODTNVRAM;C(nv->attach(gRoot));auto image=fixture();auto data=OSData::withBytes(image.data(),image.size());C(gChosen->setProperty("nvram-proxy-data",data.get()));prop32(gChosen,"nvram-bank-size",8192);prop32(gChosen,"nvram-bank-count",1);prop32(gChosen,"nvram-current-bank",0);prop32(gChosen,"k70-nvram-proxy-ro",1);
 IOReturn expected=kIOReturnSuccess;IOService*arg=nv;OtherController*hidden=nullptr;
 if(test=="new-fail"){fail_new=true;expected=kIOReturnNoMemory;}
 else if(test=="iteration-cap"){iter_overflow=true;expected=kIOReturnBusy;}
 else if(test=="activation-denied"){gActivation399.claimed=1;expected=kIOReturnExclusiveAccess;}
 else if(test=="prebind-no-read"){skip_reload=true;expected=kIOReturnExclusiveAccess;}
 else if(test=="hidden-controller"){hidden=new OtherController;nv->controller=hidden;expected=kIOReturnExclusiveAccess;}
 else if(test=="null-provider"){arg=nullptr;expected=kIOReturnBadArgument;}
 else if(test=="platform-parent"){nv->detach(gRoot);C(nv->attach(gPlatform));expected=kIOReturnBadArgument;}
 else if(test=="other-root-parent"){auto*p=new IOPlatformExpertDevice;nv->detach(gRoot);C(nv->attach(p));p->release();expected=kIOReturnBadArgument;}
 else if(test=="wrong-class"){arg=gPlatform;expected=kIOReturnBadArgument;}
 else if(test=="off-plane"){nv->inplane=false;expected=kIOReturnBadArgument;}
 else if(test=="optin-off"){prop32(gChosen,"k70-nvram-proxy-ro",0);expected=kIOReturnNotReady;}
 else if(test=="optin-short"){uint8_t b=1;auto d=OSData::withBytes(&b,1);C(gChosen->setProperty("k70-nvram-proxy-ro",d.get()));expected=kIOReturnBadArgument;}
 else if(test=="optin-type"){C(gChosen->setProperty("k70-nvram-proxy-ro",kOSBooleanTrue));expected=kIOReturnBadArgument;}
 else if(test=="geometry-short"){uint8_t b=0;auto d=OSData::withBytes(&b,1);C(gChosen->setProperty("nvram-bank-size",d.get()));expected=kIOReturnBadArgument;}
 else if(test=="multibank"){prop32(gChosen,"nvram-bank-count",2);expected=kIOReturnUnsupported;}
 else if(test=="bad-checksum"){data->b[100]^=1;expected=kIOReturnBadArgument;}
 else if(test=="proxy-type"){C(gChosen->setProperty("nvram-proxy-data",kOSBooleanTrue));expected=kIOReturnNotReady;}
 else if(test=="already-resource"){C(gResources->setProperty("IONVRAM",kOSBooleanFalse));expected=kIOReturnExclusiveAccess;}
 else if(test=="already-controller"){auto*c=new OtherController;C(c->attach(gRoot));expected=kIOReturnExclusiveAccess;}
 else if(test=="controller-at-recheck"){controller_on_second_iter=true;expected=kIOReturnExclusiveAccess;}
 else if(test=="iterator-fail"){fail_iter=true;expected=kIOReturnNotReady;}
 else if(test=="iterator-invalid"){invalid_iter=true;expected=kIOReturnBusy;}
 else if(test=="topology-change"){change_generation=true;expected=kIOReturnBusy;}
 else if(test=="copy-fail"){fail_copy=true;expected=kIOReturnNoMemory;}
 else if(test=="metadata-fail"){fail_prop=true;expected=kIOReturnNoMemory;}
 else if(test=="init-fail"){fail_init=true;expected=kIOReturnNoMemory;}
 else if(test=="attach-fail"){fail_attach=true;expected=kIOReturnNoResources;}
 else if(test=="start-fail"){fail_start=true;expected=kIOReturnNotReady;}
 else C(test=="success");
 bool pinnedFailure=hidden||test=="activation-denied"||test=="prebind-no-read";
 int liveBefore=gLive,refsBefore=nv->refs;size_t entriesBefore=gEntries.size();
 C(!k70_nvram_ro_active399());C(!k70_nvram_ro_for_provider399(nv));
 auto result=k70_nvram_ro_create399(arg);C(result==expected);
 if(expected){C(!k70_nvram_ro_active399());C(!k70_nvram_ro_for_provider399(nv));C(gBaseCalls==0);if(pinnedFailure){C(nv->refs==refsBefore+2);if(hidden)C(nv->controller==hidden);else C(nv->controller!=nullptr);C(nv->reloads==1);C(nv->cacheMarker==0x12345678);}else C(nv->refs==refsBefore);if(!controller_on_second_iter&&!pinnedFailure){C(gLive==liveBefore);C(gEntries.size()==entriesBefore);}}
 else{
 C(gBaseCalls==1);C(k70_nvram_ro_active399());C(k70_nvram_ro_for_provider399(nv));C(!k70_nvram_ro_for_provider399(gPlatform));C(nv->refs==refsBefore+2);
 auto*c=gInstalled399;C(c&&c->getProvider()==nv);C(nv->controller==c);C(nv->reloads==2);C(nv->cacheMarker==0x12345678);uint8_t h[32]={};C(c->read(0,h,32)==0);C(!memcmp(h,image.data(),32));data->b[0]=0;memset(h,0,32);C(c->read(0,h,32)==0);C(h[0]==0x70); // actual deep copy
 C(c->write(0,h,32)==kIOReturnNotWritable);C(c->write(UINT64_MAX,nullptr,UINT64_MAX)==kIOReturnNotWritable);C(c->eraseBank()==kIOReturnNotWritable);C(c->select(1)==kIOReturnBadArgument);C(c->read(8192,h,1)==kIOReturnBadArgument);C(c->read(0,nullptr,1)==kIOReturnBadArgument);c->sync();C(k70_nvram_ro_counters399[6]==1);
 int apiBefore=gAPIs;for(unsigned i=0;i<10000;i++){C(k70_nvram_ro_active399());C(k70_nvram_ro_for_provider399(nv));C(!k70_nvram_ro_for_provider399(gPlatform));C(!k70_nvram_ro_for_provider399(nullptr));}C(gAPIs==apiBefore);
 C(k70_nvram_ro_create399(nv)==kIOReturnExclusiveAccess);C(gBaseCalls==1);
 // Test-process teardown only; production intentionally provides NO unload path.
 c->stop(nv);c->detach(nv);c->release();gInstalled399=nullptr;
 }
 fail_new=fail_prop=fail_attach=fail_start=false;
 // Remove external controller fixture, if any; iterator never owns those refs.
 for(auto*e:std::vector<IORegistryEntry*>(gEntries)){if(auto*c=dynamic_cast<OtherController*>(e)){c->detach(c->provider);c->release();}else if(auto*c=dynamic_cast<NativeProxyNVRAM399*>(e)){C(pinnedFailure);c->stop(nv);c->detach(nv);c->release();}}
 if(hidden)hidden->release();
 auto*parent=nv->provider;nv->detach(parent);nv->release();gChosen->release();gChosen=nullptr;gResources->release();gRoot->release();gPlatform->release();
 // data local retains one object until destruction; booleans are fixture globals.
 C(gLive==3);kOSBooleanTrue->release();kOSBooleanFalse->release();C(gLive==1);
 printf("scenario=%s checks=%u\n",test.c_str(),checks);
}
