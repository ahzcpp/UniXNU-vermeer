"""Run actual extracted setter/remove/sync bodies against bounded host fixtures.

Fixtures model OS objects and permissions, not firmware, IOKit scheduling or
real persistence. Exercise legacy delete error swallowing and sync false-success
regressions, preserving original permission/type/size errors and other providers.
"""
from pathlib import Path
import hashlib,json,re,subprocess
O=Path('/mnt/d/K70-UniXNU-data/nvram-core401/v2');W=O/'host';W.mkdir(exist_ok=True)
def body(s,name):
    at=s.index(name+'(');start=s.index('{',at);i=start+1;depth=1
    while depth:
        if s[i]=='{':depth+=1
        if s[i]=='}':depth-=1
        i+=1
    return s[start:i]
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstddef>
using IOReturn=int32_t;using uuid_t=unsigned char[16];
static constexpr IOReturn kIOReturnSuccess=0,kIOReturnError=-1,kIOReturnNotPrivileged=-2,kIOReturnBadArgument=-3,kIOReturnNoSpace=-4,kIOReturnNotFound=-5,kIOReturnNotWritable=-6;
static constexpr int kIONVRAMOperationWrite=1,kIONVRAMOperationDelete=2,OSRetain=1;
enum {kOFVariableTypeBoolean,kOFVariableTypeNumber,kOFVariableTypeString,kOFVariableTypeData};
static constexpr size_t BOOT_LINE_LENGTH=1000;
static const char kIONVRAMDeletePropertyKey[]="IONVRAM-DELETE-PROPERTY",kIONVRAMDeletePropertyKeyWRet[]="IONVRAM-DELETE-PROPERTY-WRET",kIONVRAMSyncNowPropertyKey[]="IONVRAM-SYNCNOW-PROPERTY",kIONVRAMForceSyncNowPropertyKey[]="IONVRAM-FORCESYNCNOW-PROPERTY",kIONVRAMBootArgsKey[]="boot-args",kCurrentGenerationCountKey[]="Generation";
#define DEBUG_INFO(...) ((void)0)
#define DEBUG_ERROR(...) ((void)0)
#define record_system_event(...) ((void)0)
#define NVRAMWRITELOCK() ((void)0)
#define NVRAMREADLOCK() ((void)0)
#define NVRAMUNLOCK() ((void)0)
#define CONTROLLERLOCK() ((void)0)
#define CONTROLLERUNLOCK() ((void)0)
static bool canlock=true;
#define SAFE_TO_LOCK() canlock
struct OSObject{virtual ~OSObject()=default;};
struct OSBoolean:OSObject{bool getValue(){return true;}};
struct OSNumber:OSObject{uint64_t unsigned64BitValue(){return 4;}};
struct OSString:OSObject{const char*v;size_t n;explicit OSString(const char*s,size_t z=0):v(s),n(z?z:std::strlen(s)){}const char*getCStringNoCopy(){return v;}size_t getLength(){return n;}};
struct OSData:OSObject{size_t n=4;size_t getLength(){return n;}static OSData*withBytes(const char*,size_t z){static OSData d;d.n=z;return &d;}};
template<class T>struct OSSharedPtr{T*p=nullptr;OSSharedPtr()=default;OSSharedPtr(T*x,int=0):p(x){}T*get()const{return p;}T*operator->()const{return p;}explicit operator bool()const{return p!=nullptr;}bool operator==(std::nullptr_t)const{return p==nullptr;}bool operator!=(std::nullptr_t)const{return p!=nullptr;}void reset(){p=nullptr;}OSSharedPtr&operator=(T*x){p=x;return *this;}template<class U>OSSharedPtr&operator=(const OSSharedPtr<U>&x){p=x.get();return *this;}};
template<class T,class U>OSSharedPtr<T>OSDynamicPtrCast(const OSSharedPtr<U>&p){return dynamic_cast<T*>(p.get());}
#define OSDynamicCast(t,p) dynamic_cast<t*>(p)
static bool active=false,permission=true,sizeok=true;static const void*owner=nullptr;static int desiredType=kOFVariableTypeData,permissions=0;
extern "C" bool k70_nvram_ro_for_provider399(const void*p){return active&&p==owner;}
static bool verifyPermission(int,const uuid_t,const char*,bool){permissions++;return permission;}
static bool verifyWriteSizeLimit(const uuid_t,const char*,size_t){return sizeok;}
static int getVariableType(const char*){return desiredType;}
static bool parseVariableName(const char*s,uuid_t*g,const char**v){std::memset(g,0,16);*v=s;return false;}
static void translateGUID(const uuid_t a,const char*,uuid_t b,bool){std::memcpy(b,a,16);}
struct Format{int sets=0,syncs=0,generation=7;bool dirty=true;OSObject*cached=nullptr;IOReturn writeResult=kIOReturnSuccess;
 bool getSystemPartitionActive(){return false;}unsigned getGeneration(){return generation;}
 IOReturn setVariable(const uuid_t,const char*,OSObject*v){sets++;cached=v;dirty=true;return writeResult;}
 IOReturn sync(){syncs++;dirty=false;generation++;return kIOReturnSuccess;}};
struct Diags{void setProperty(const char*,OSObject*){}};
struct DT{Format fmt;Format*_format=&fmt;void*_varDict=this;Diags*_diags=nullptr;bool rate=true;int special=0;
 bool safeToSync(){return rate;}
 bool handleSpecialVariables(const char*n,const uuid_t,OSObject*,IOReturn*r){if(std::strcmp(n,"ResetNVRam")==0||std::strcmp(n,"ObliterateNVRam")==0){special++;fmt.dirty=true;*r=0;return true;}return false;}
 IOReturn setPropertyWithGUIDAndName(const uuid_t,const char*,OSObject*);IOReturn removePropertyWithGUIDAndName(const uuid_t,const char*);IOReturn syncInternal(bool);};
'''
# syncInternal's diagnostic OSNumber allocation is also extracted verbatim.
pre=pre.replace('struct OSNumber:OSObject{','struct OSNumber:OSObject{static OSSharedPtr<OSNumber>withNumber(uint32_t,unsigned);')
pre=pre.replace('struct OSObject{','template<class T>struct OSSharedPtr;\nstruct OSObject{')
pre += '\nOSSharedPtr<OSNumber> OSNumber::withNumber(uint32_t,unsigned){static OSNumber n;return &n;}\n'
test=r'''
static unsigned checks;
#define CK(x) do{checks++;assert(x);}while(0)
int main(){uuid_t guid={};OSBoolean b;OSNumber n;OSString s("value");OSData d;OSObject wrong;OSObject*valid[]={&b,&n,&s,&d};
 for(int enabled=0;enabled<2;enabled++)for(int bound=0;bound<2;bound++)for(int grant=0;grant<2;grant++)for(int typ=0;typ<4;typ++)for(int good=0;good<2;good++)for(int fits=0;fits<2;fits++){
  DT a,other;active=enabled;owner=bound?&a:&other;permission=grant;desiredType=typ;sizeok=fits;permissions=0;
  bool ro=enabled&&bound;OSObject*obj=good?valid[typ]:&wrong;IOReturn actual=a.setPropertyWithGUIDAndName(guid,"sample",obj);
  IOReturn expected=!grant?kIOReturnNotPrivileged:!good?kIOReturnBadArgument:!fits?kIOReturnNoSpace:ro?kIOReturnNotWritable:kIOReturnSuccess;
  CK(actual==expected);CK(permissions==1);CK(a.fmt.sets==(expected==0));CK(a.fmt.cached==(expected==0?obj:nullptr));
 }
 for(int enabled=0;enabled<2;enabled++)for(int bound=0;bound<2;bound++)for(int grant=0;grant<2;grant++)for(int have=0;have<2;have++){
  DT a,other;active=enabled;owner=bound?&a:&other;permission=grant;a._varDict=have?&a:nullptr;permissions=0;
  IOReturn r=a.removePropertyWithGUIDAndName(guid,"sample");bool ro=enabled&&bound;
  CK(r==(!have?kIOReturnNotFound:!grant?kIOReturnNotPrivileged:ro?kIOReturnNotWritable:0));CK(a.fmt.sets==(!have||!grant||ro?0:1));CK(permissions==(have?1:0));
 }
 permission=true;sizeok=true;desiredType=kOFVariableTypeData;
 for(int enabled=0;enabled<2;enabled++)for(int bound=0;bound<2;bound++)for(int locked=0;locked<2;locked++)for(int limited=0;limited<2;limited++)for(int throttle=0;throttle<2;throttle++){
  DT a,other;active=enabled;owner=bound?&a:&other;canlock=locked;a.rate=!limited;bool ro=enabled&&bound;
  for(int twice=0;twice<2;twice++)CK(a.syncInternal(throttle)==(ro?kIOReturnNotWritable:0));
  int calls=ro||!locked||(throttle&&limited)?0:2;CK(a.fmt.syncs==calls);CK(a.fmt.generation==7+calls);CK(a.fmt.dirty==(calls==0));
 }
 canlock=true;
 // Regression: the old delete alias used to swallow every error; genuine RO
 // deletes must now fail while preserving the old non-RO alias behavior.
 for(const char*key:{kIONVRAMDeletePropertyKey,kIONVRAMDeletePropertyKeyWRet})for(int enabled=0;enabled<2;enabled++){
  DT a;active=enabled;owner=&a;OSString target("sample");
  CK(a.setPropertyWithGUIDAndName(guid,key,&target)==(enabled?kIOReturnNotWritable:0));CK(a.fmt.sets==(enabled?0:1));
 }
 for(const char*key:{"ResetNVRam","ObliterateNVRam"})for(int enabled=0;enabled<2;enabled++){
  DT a;active=enabled;owner=&a;CK(a.setPropertyWithGUIDAndName(guid,key,&d)==(enabled?kIOReturnNotWritable:0));CK(a.special==(enabled?0:1));
 }
 for(const char*key:{kIONVRAMSyncNowPropertyKey,kIONVRAMForceSyncNowPropertyKey}){
  DT a;active=true;owner=&a;OSString token("now");a.rate=false;
  CK(a.setPropertyWithGUIDAndName(guid,key,&token)==kIOReturnNotWritable);CK(a.fmt.syncs==0&&a.fmt.dirty);
  CK(a.setPropertyWithGUIDAndName(guid,key,&wrong)==kIOReturnError);
 }
 {DT a;active=true;owner=&a;desiredType=kOFVariableTypeString;OSString tooLong("unused",BOOT_LINE_LENGTH);CK(a.setPropertyWithGUIDAndName(guid,"boot-args",&tooLong)==kIOReturnNoSpace);CK(a.fmt.sets==0);}
 std::printf("PASS401 %u assertions; exact setter/remove/sync bodies; host fixtures only\n",checks);
}
'''
fragments={}
for name,sig in [('setPropertyWithGUIDAndName','const uuid_t guid,const char*name,OSObject*anObject'),('removePropertyWithGUIDAndName','const uuid_t guid,const char*name'),('syncInternal','bool rateLimit')]:
    fragments[name]=body((O/'candidate/IONVRAM.cpp').read_text(),'IODTNVRAM::'+name)
source=pre+'\n'+''.join('IOReturn DT::'+k+'('+sig+')'+fragments[k]+'\n' for k,sig in [('setPropertyWithGUIDAndName','const uuid_t guid,const char*name,OSObject*anObject'),('removePropertyWithGUIDAndName','const uuid_t guid,const char*name'),('syncInternal','bool rateLimit')])+test
source='#include <initializer_list>\n'+source
(W/'guards.cpp').write_text(source)
cmd=['clang++','-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(W/'guards.cpp'),'-o',str(W/'guards')]
r=subprocess.run(cmd,text=True,capture_output=True);(W/'build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(W/'guards')],text=True,capture_output=True);(W/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
report=dict(status='PASS_EXTRACTED_CORE_GUARDS',assertions=int(re.search(r'PASS401 (\d+)',r.stdout)[1]),source_fragments={k:hashlib.sha256(v.encode()).hexdigest() for k,v in fragments.items()},sanitizers=['ASan','UBSan'],limitations='OS objects/locks/permissions/format backend are host fixtures; no IOKit registration, real bank, persistence, AppleImage4 runtime, device or complete serializer tested here',hardware_tested=False)
(O/'host-proof.json').write_text(json.dumps(report,indent=2)+'\n');print(r.stdout,end='')
