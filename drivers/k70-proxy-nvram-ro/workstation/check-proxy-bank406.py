"""Validate actual394 ADT proxy bank against native399 portable validation core.

Only scalar geometry/hashes emitted. Private bank copy stays local on D.
"""
from pathlib import Path
import hashlib,importlib.util,json,struct,subprocess
P=Path(__file__).resolve().parent;D=Path('/mnt/d/K70-UniXNU-data');O=D/'nvram-bank406';O.mkdir(exist_ok=True)
sp=importlib.util.spec_from_file_location('adt',P/'build-test60-payload.py');reader=importlib.util.module_from_spec(sp);sp.loader.exec_module(reader)
adt=subprocess.check_output(['mtype','-i',str(D/'builds/gpu394-payload/payload.img'),'::/EFI/UniXNU/Payloads/kernel.macho.adt'])
def walk(node):
    if reader.name(node)=='chosen':return node
    for child in node[1]:
        found=walk(child)
        if found:return found
node=walk(reader.parse(adt));assert node
props={k:v for k,flags,v in node[0]};geometry=[struct.unpack('<I',props[k])[0] for k in ('nvram-bank-size','nvram-bank-count','nvram-current-bank')]
bank=props['nvram-proxy-data'];assert len(bank)==geometry[0];private=O/'private';private.mkdir(exist_ok=True);(private/'proxy-bank.bin').write_bytes(bank)
source=r'''
#include "ProxyBank399.h"
#include <vector>
#include <fstream>
#include <iterator>
#include <cassert>
#include <cstdio>
int main(int argc,char**argv){assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});k70ro399::Bank out{};
 auto result=k70ro399::validate(b.data(),b.size(),{GEOMETRY},&out);
 std::printf("validation_status=%u version=%u ready=%u bytes=%zu\n",unsigned(result),out.version,unsigned(out.ready),b.size());
 return result==k70ro399::ok?0:1;}
'''.replace('GEOMETRY',','.join(str(x) for x in geometry))
core=D/'wifi-agent322/work/nvram-controller399/src/ProxyBank399.h';core_hash=hashlib.sha256(core.read_bytes()).hexdigest()
(O/'check.cpp').write_text(source);cmd=['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(core.parent),str(O/'check.cpp'),'-o',str(O/'check')]
r=subprocess.run(cmd,text=True,capture_output=True);(O/'compile.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
r=subprocess.run([str(O/'check'),str(private/'proxy-bank.bin')],text=True,capture_output=True);(O/'run.log').write_text(r.stdout+r.stderr)
assert core_hash==hashlib.sha256(core.read_bytes()).hexdigest(),'Input changed while validating'
report=dict(status='PASS_ACTUAL_PROXY_BANK' if r.returncode==0 else 'FAIL_ACTUAL_PROXY_BANK',hardware_tested=False,source_boot=394,source_ADT_sha256=hashlib.sha256(adt).hexdigest(),geometry=geometry,bank_sha256=hashlib.sha256(bank).hexdigest(),bank_length=len(bank),validator_header_sha256=core_hash,sanitizers=['ASan','UBSan'],result=r.stdout.strip(),private_bytes_not_published=True)
(O/'proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));assert r.returncode==0,r.stderr
