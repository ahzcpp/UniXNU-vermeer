"""Regression: a rejected RO sync never clears dirty/image/generation.

Reuse the reviewed397 host fixture construction without executing/writing397's
build/tests. Serializer's image preparation is modeled; actual entry guards and
controller/error/dirty tail are extracted. This is explicitly not a full parser
or persistence test.
"""
from pathlib import Path
import hashlib,json,re,subprocess
O=Path('/mnt/d/K70-UniXNU-data/nvram-core401/v2');W=O/'serializer-host';W.mkdir(exist_ok=True)
original=Path('/mnt/d/K70-UniXNU-data/wifi-agent322/work/nvram-ro397/run_contract_tests.py').read_text()
scope={'__file__':str(W/'fixture401.py')}
exec(original[:original.index("body='IOReturn CHRPFixture::serializeVariables()")],scope)
function=scope['function'];pre=scope['pre'];s=(O/'candidate/IONVRAMCHRPHandler.cpp').read_text();ser=function(s,'IONVRAMCHRPHandler::serializeVariables')
prefix=ser[ser.index('\tif (k70_nvram_ro_for_provider399(_provider))'):ser.index('nvramImage = IONewZeroData')]
tail=ser[ser.index('nvram_set_apple_header'):ser.rfind('}')]
getter='static bool active401;static const void*owner401;extern "C" bool k70_nvram_ro_for_provider399(const void*p){return active401&&p==owner401;}\n'
body='IOReturn CHRPFixture::serializeVariables(){IOReturn ret;uint8_t*nvramImage;\n'+prefix+'nvramImage=static_cast<uint8_t*>(std::calloc(_bankSize,1));assert(nvramImage);\n'+tail+'}\n'
test=r'''
static unsigned checks;
#define CK(x) do{checks++;assert(x);}while(0)
int main(){
 for(int enabled=0;enabled<2;enabled++)for(int bound=0;bound<2;bound++)for(int dirty=0;dirty<2;dirty++)for(int ready=0;ready<2;ready++)for(int image=0;image<2;image++)for(int sized=0;sized<2;sized++){
  BackendFixture b;CHRPFixture f(&b);Provider other;active401=enabled;owner401=bound?&f.provider:&other;
  f._newData=dirty;f._generation=19;if(!ready)f._nvramController=nullptr;if(!sized)f._bankSize=0;
  if(!image){std::free(f._nvramImage);f._nvramImage=nullptr;}
  uint8_t*before=f._nvramImage;bool ro=enabled&&bound;
  IOReturn first=f.serializeVariables();
  if(ro){CK(first==kIOReturnNotWritable);CK(f._newData==bool(dirty));CK(f._generation==19);CK(f._nvramImage==before);CK(b.writes==0&&b.erases==0&&b.selects==0&&b.syncs==0);CK(f.serializeVariables()==kIOReturnNotWritable);CK(f._newData==bool(dirty));}
  else {IOReturn expected=!ready?kIOReturnNotReady:!dirty||!sized||!image?kIOReturnSuccess:kIOReturnNotWritable;CK(first==expected);CK(b.writes==(ready&&dirty&&sized&&image?1u:0u));}
 }
 // Direct serializer must retain a pending dirty write on each RO retry,
 // even though unmodified CHRP would clear it after the first failed write.
 {BackendFixture b;CHRPFixture f(&b);active401=true;owner401=&f.provider;uint8_t*image=f._nvramImage;
  for(int i=0;i<50;i++){CK(f.serializeVariables()==kIOReturnNotWritable);CK(f._newData&&f._nvramImage==image&&f._generation==0);}
  CK(b.writes==0&&b.syncs==0);}
 std::printf("PASS401_SERIALIZER %u assertions; real guard/error-tail fragments; host preparation modeled\n",checks);
}
'''
source=pre+getter+body+test;(W/'serializer.cpp').write_text(source)
cmd=['clang++','-std=c++17','-g','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(W/'serializer.cpp'),'-o',str(W/'serializer')]
r=subprocess.run(cmd,text=True,capture_output=True);(W/'build.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(W/'serializer')],text=True,capture_output=True);(W/'run.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
(O/'serializer-host-proof.json').write_text(json.dumps(dict(status='PASS_RO_DIRTY_IMAGE_GENERATION_RETAINED',assertions=int(re.search(r'SERIALIZER (\d+)',r.stdout)[1]),cases=64,retries=50,sanitizers=['ASan','UBSan'],entry_guard_fragment_sha256=hashlib.sha256(prefix.encode()).hexdigest(),tail_fragment_sha256=hashlib.sha256(tail.encode()).hexdigest(),hardware_tested=False,limitations='Actual entry guard and initial checks + actual controller tail; allocation/CRC/serialization and containers are host models, no bank validation/real persistence or IOKit registration'),indent=2)+'\n')
print(r.stdout,end='')
