"""Public synthetic fixtures for native binary-plist parsing and detached signing.
No OS cache, firmware, credentials, original executable or signing key is input.
"""
from pathlib import Path
import ctypes,hashlib,json,plistlib,struct,subprocess,sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
run=lambda *a:subprocess.run(list(map(str,a)),check=True,capture_output=True,text=True)
xpc=ctypes.CDLL('/usr/lib/system/libxpc.dylib')
xpc.xpc_create_from_plist.argtypes=[ctypes.c_void_p,ctypes.c_size_t];xpc.xpc_create_from_plist.restype=ctypes.c_void_p
xpc.xpc_dictionary_get_value.argtypes=[ctypes.c_void_p,ctypes.c_char_p];xpc.xpc_dictionary_get_value.restype=ctypes.c_void_p
xpc.xpc_dictionary_get_count.argtypes=[ctypes.c_void_p];xpc.xpc_dictionary_get_count.restype=ctypes.c_size_t
xpc.xpc_release.argtypes=[ctypes.c_void_p]
def parse_xpc(raw,count):
    buf=ctypes.create_string_buffer(raw);p=xpc.xpc_create_from_plist(buf,len(raw));assert p
    try:
        jobs=xpc.xpc_dictionary_get_value(p,b'LaunchDaemons');assert jobs
        assert xpc.xpc_dictionary_get_count(jobs)==count
    finally:xpc.xpc_release(p)
def inspect(raw,signature):
    records=[]
    def read(data,level=0):
        assert level<5 and len(data)>=8
        magic,n=struct.unpack_from('>II',data);assert n==len(data)
        if magic in (0xfade0cc0,0xfade0cc1):
            count=struct.unpack_from('>I',data,8)[0];assert count<64 and 12+8*count<=n
            for i in range(count):
                slot,at=struct.unpack_from('>II',data,12+8*i);assert at>=12+8*count and at+8<=n
                size=struct.unpack_from('>I',data,at+4)[0];assert 8<=size<=n-at
                read(data[at:at+size],level+1)
        elif magic==0xfade0c02:
            _,_,version,flags,hashoff,identoff,special,slots,limit=struct.unpack_from('>9I',data)
            hs,ht,platform,pg=struct.unpack_from('4B',data,36)
            assert ht in (1,2,3,4) and pg==12 and limit==len(raw)
            assert slots==(len(raw)+4095)//4096 and 0<hs<=48 and hashoff>=special*hs and hashoff+slots*hs<=n
            algo={1:hashlib.sha1,2:hashlib.sha256,3:hashlib.sha256,4:hashlib.sha384}[ht]
            for i in range(slots):assert data[hashoff+i*hs:hashoff+(i+1)*hs]==algo(raw[i*4096:(i+1)*4096]).digest()[:hs]
            records.append(dict(version=version,flags=flags,hash_offset=hashoff,identifier_offset=identoff,special_slots=special,code_slots=slots,code_limit=limit,hash_size=hs,hash_type=ht,platform=platform,page_shift=pg,cdhash=algo(data).digest()[:20].hex(),header_hex=data[:hashoff-special*hs].hex(),cd_hex=data.hex()))
    read(signature);assert records
    return records
reports=[]
for count in (1,3,533):
    jobs={f'/System/Library/LaunchDaemons/org.unixnu.fixture.{i}.plist':dict(Label=f'org.unixnu.fixture.{i}',ProgramArguments=['/usr/libexec/fixture','-q'],RunAtLoad=True,KeepAlive=True,ThrottleInterval=3,UserName='root',Umask=63,ProcessType='Interactive') for i in range(count)}
    value=dict(VersionNumber=7,LaunchDaemons=jobs)
    python_raw=plistlib.dumps(value,fmt=plistlib.FMT_BINARY,sort_keys=False);parse_xpc(python_raw,count)
    p=out/f'fixture-{count}.plist';p.write_bytes(python_raw)
    run('plutil','-convert','binary1',p);native=p.read_bytes();assert plistlib.loads(native)==value;parse_xpc(native,count)
    sig=p.with_suffix('.plist.sig')
    run('codesign','--force','--sign','-','--pagesize','4096','--identifier','org.unixnu.fixture.launchd-cache','--detached',sig,p)
    run('codesign','--verify','--strict','--detached',sig,p)
    reports.append(dict(count=count,python_sha256=hashlib.sha256(python_raw).hexdigest(),native_sha256=hashlib.sha256(native).hexdigest(),bytes=len(native),python_binary_xpc_accepted=True,native_binary_xpc_accepted=True,signature_sha256=hashlib.sha256(sig.read_bytes()).hexdigest(),directories=inspect(native,sig.read_bytes())))
(out/'report.json').write_text(json.dumps(dict(synthetic_only=True,private_inputs=False,native_codesign_verified=True,fixture_reports=reports),indent=2)+'\n')
print('PASS: synthetic plistlib/native binary1 XPC parsing and native detached CodeDirectory page hashes')
