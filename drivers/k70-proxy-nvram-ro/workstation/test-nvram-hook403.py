"""Extract two actual PE methods to check selector/order/options regressions.

IOKit objects, iterator and factory are host fixtures. This tests call ordering
and admission behavior, not IOKit locks, registration, bank validity or hardware.
"""
from pathlib import Path
import hashlib,json,re,subprocess
O=Path('/mnt/d/K70-UniXNU-data/nvram-hook403');W=O/'host';W.mkdir(exist_ok=True)
s=(O/'candidate/IOPlatformExpert.cpp').read_text()
def body(name):
    at=s.index(name);start=s.index('{',at);i=start+1;depth=1
    while depth:
        if s[i]=='{':depth+=1
        if s[i]=='}':depth-=1
        i+=1
    return s[start:i]
process=body('IODTPlatformExpert::processTopLevel(');create=body('IODTPlatformExpert::createNub(')
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using UInt8=uint8_t;using UInt32=uint32_t;using SInt32=int32_t;
static constexpr int gIODTPlane=1,kIODTExclusive=2;
struct OSObject{virtual ~OSObject()=default;};
struct OSData:OSObject{std::vector<UInt8>v;bool bytes=true;unsigned getLength(){return v.size();}const void*getBytesNoCopy(){return bytes?v.data():nullptr;}};
struct OSIterator;
static std::vector<int>events;static OSObject*selector;static bool chosenPresent=true,active=false,factoryOK=true,nubInitOK=true;static unsigned factories=0,news=0;static const void*owner;
struct IORegistryEntry:OSObject{int refs=0;static IORegistryEntry*fromPath(const char*,int);OSObject*getProperty(const char*){return selector;}void release(){refs--;}void detachAll(int){events.push_back(1);}IORegistryEntry*childFromPath(const char*,int){return nullptr;}};
static IORegistryEntry chosen;
IORegistryEntry*IORegistryEntry::fromPath(const char*,int){if(!chosenPresent)return nullptr;chosen.refs++;return &chosen;}
struct IOService:IORegistryEntry{bool init(IORegistryEntry*,int){return nubInitOK;}void free(){delete this;}};
struct IOPlatformDevice:IOService{IOPlatformDevice(){news++;}};
struct OSIterator:OSObject{OSObject*getNextObject(){return nullptr;}void release(){}};
static OSIterator*IODTFindMatchingEntries(IORegistryEntry*,int,const char*){return nullptr;}
static IOService*gIOOptionsEntry;
#define OSDynamicCast(t,x) dynamic_cast<t*>(x)
#define IOLog(...) ((void)0)
struct IODTPlatformExpert:IOService{IOService*dtNVRAM=nullptr;const char*deleteList(){return "()";}const char*excludeList(){return "()";}void publishNVRAM(){events.push_back(2);}void createNubs(IOService*,OSIterator*){events.push_back(4);}void processTopLevel(IORegistryEntry*);IOService*createNub(IORegistryEntry*);};
static IODTPlatformExpert*current;
extern "C" SInt32 k70_nvram_ro_create399(IOService*p){factories++;events.push_back(3);assert(current->dtNVRAM==p&&p==gIOOptionsEntry);if(!factoryOK)return -1;active=true;owner=p;return 0;}
extern "C" bool k70_nvram_ro_for_provider399(const void*p){return p&&active&&p==owner;}
'''
test=r'''
static unsigned checks;
#define CK(x) do{checks++;assert(x);}while(0)
int main(){OSData data;OSObject wrong;IOService options,other;gIOOptionsEntry=&options;
 for(int present=0;present<2;present++)for(int typ=0;typ<3;typ++)for(unsigned len=0;len<9;len++)for(int value=0;value<4;value++)for(int success=0;success<2;success++){
  IODTPlatformExpert pe;current=&pe;events.clear();factories=0;news=0;active=false;owner=nullptr;factoryOK=success;chosenPresent=present;chosen.refs=0;
  data.v.assign(len,0);if(len)data.v[0]=value==3?1:value;if(value==3&&len>1)data.v[1]=1;data.bytes=true;selector=typ==0?nullptr:typ==1?&wrong:&data;
  pe.processTopLevel(&pe);bool requested=present&&typ==2&&len==4&&value==1;
  CK(factories==unsigned(requested));CK(pe.dtNVRAM==&options);CK(chosen.refs==0);CK(active==(requested&&success));CK(events.front()==2&&events.back()==4);
  if(requested)CK(events==std::vector<int>({2,3,4}));else CK(events==std::vector<int>({2,4}));
  IOService*n=pe.createNub(&options);CK((n==nullptr)==(requested&&success));if(n)n->free();
  n=pe.createNub(&other);CK(n!=nullptr);n->free();
 }
 // A malformed/null byte backing refuses enrollment even with the correct length.
 {IODTPlatformExpert pe;current=&pe;chosenPresent=true;data.v={1,0,0,0};data.bytes=false;selector=&data;active=false;factories=0;pe.processTopLevel(&pe);CK(factories==0&&!active);}
 // The original allocation/init-failure path for unrelated nodes survives.
 {IODTPlatformExpert pe;nubInitOK=false;active=true;owner=&options;CK(pe.createNub(&other)==nullptr);CK(pe.createNub(&options)==nullptr);}
 std::printf("PASS403 %u assertions; actual two PE bodies with host IOKit/factory fixtures\n",checks);
}
'''
source=pre+'\nvoid IODTPlatformExpert::processTopLevel(IORegistryEntry*rootEntry)'+process+'\nIOService*IODTPlatformExpert::createNub(IORegistryEntry*from)'+create+test
(W/'hooks.cpp').write_text(source)
cmd=['clang++','-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(W/'hooks.cpp'),'-o',str(W/'hooks')]
r=subprocess.run(cmd,text=True,capture_output=True);(W/'build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(W/'hooks')],text=True,capture_output=True);(W/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
(O/'host-proof.json').write_text(json.dumps(dict(status='PASS_EXTRACTED_SELECTOR_ORDER_OPTIONS',assertions=int(re.search(r'PASS403 (\d+)',r.stdout)[1]),selector_cases=432,source_fragment_sha256={'process':hashlib.sha256(process.encode()).hexdigest(),'create':hashlib.sha256(create.encode()).hexdigest()},sanitizers=['ASan','UBSan'],hardware_tested=False,limitations='IOKit objects/iterators/factory are fixtures. Not a registration, locking, actualcontroller, bank, persistence or device test.'),indent=2)+'\n')
print(r.stdout,end='')
