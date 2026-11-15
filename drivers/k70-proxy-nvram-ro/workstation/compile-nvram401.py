"""Compile isolated provider-scoped RO guards with actual XNU target recipes.

No shared source tree edits, phone access, controller registration or persistence.
The new external predicate must be supplied by the separately reviewed 399 driver.
"""
from pathlib import Path
import difflib, hashlib, json, os, subprocess

P = Path(__file__).resolve().parent
O = Path('/mnt/d/K70-UniXNU-data/nvram-core401/v2')
T = Path('/root/k70-clean-20261104/storage-candidate-20261104/out/vermeer/ipados-17.7.11-21H461/vermeer/tree')
SRC = T / '.cache/xnu-work'
N = T / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
O.mkdir(parents=True,exist_ok=True)
assert not (O/'compile-review.json').exists(), 'Frozen completed build'
H = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()

def inject_before(s, anchor, addition, filename):
    assert s.count(anchor) == 1, anchor
    original=(SRC/'iokit/Kernel'/filename).read_text()
    assert original.count(anchor)==1
    line = original[:original.index(anchor)].count('\n') + 1
    return s.replace(anchor, addition + '\n#line ' + str(line) + ' "' + filename + '"\n' + anchor)

n = (SRC/'iokit/Kernel/IONVRAM.cpp').read_text()
c = (SRC/'iokit/Kernel/IONVRAMCHRPHandler.cpp').read_text()
nn = inject_before(n, '#define super IOService',
    '// K70 immutable proxy: pure acquire-load identity predicate, no locks.\n'
    'extern "C" bool k70_nvram_ro_for_provider399(const void *provider);', 'IONVRAM.cpp')
nn = inject_before(nn, '\tDEBUG_INFO("rateLimit=%d\\n", rateLimit);',
    '\t// RO rejection precedes rate-limit/unsafe-lock no-op success paths.\n'
    '\tif (k70_nvram_ro_for_provider399(this)) return kIOReturnNotWritable;', 'IONVRAM.cpp')
nn = inject_before(nn, '\tNVRAMWRITELOCK();\n\tok = handleSpecialVariables(name, guid, propObject.get(), &ret);',
    '\t// Original write permission, object type and size checks ran above.\n'
    '\t// Reject ordinary writes and reset/obliterate before any cache change.\n'
    '\tif (k70_nvram_ro_for_provider399(this)) {\n'
    '\t\tret = kIOReturnNotWritable;\n\t\tgoto exit;\n\t}', 'IONVRAM.cpp')
anchor = '\ttranslateGUID(guid, name, newGuid, _format->getSystemPartitionActive());\n\n\tNVRAMWRITELOCK();\n\n\tret = _format->setVariable(newGuid, name, nullptr);'
nn = inject_before(nn, anchor,
    '\t// Keep original dictionary/DELETE permission failures; never mutate RO.\n'
    '\tif (k70_nvram_ro_for_provider399(this)) {\n'
    '\t\tret = kIOReturnNotWritable;\n\t\tgoto exit;\n\t}', 'IONVRAM.cpp')
# The historical non-WRet delete alias swallows errors. Only the actual RO
# provider must propagate its precise error rather than report successful delete.
old = '\t\t\tif (deletePropertyKeyWRet) {\n\t\t\t\tret = removeRet;\n\t\t\t}'
new = '\t\t\tif (deletePropertyKeyWRet ||\n\t\t\t    (removeRet == kIOReturnNotWritable && k70_nvram_ro_for_provider399(this))) {\n\t\t\t\tret = removeRet;\n\t\t\t}'
assert nn.count(old) == 1
line = n[:n.index(old)].count('\n')+1
nn = nn.replace(old, new+'\n#line '+str(line+3)+' "IONVRAM.cpp"')
cc = inject_before(c,
    '\trequire_action(_nvramController != nullptr, exit, (ret = kIOReturnNotReady, DEBUG_ERROR("No _nvramController\\n")));',
    '\t// Preserve dirty state/image/generation and reject even a clean sync.\n'
    '\tif (k70_nvram_ro_for_provider399(_provider)) return kIOReturnNotWritable;',
    'IONVRAMCHRPHandler.cpp')

report = {'hardware_tested':False, 'shared_source_tree_modified':False,
          'scope':'Only active399 exact bound IODTNVRAM provider; no permission relaxation',
          'original_files':{}, 'compiles':{}}
for mode in ('baseline','candidate'):
    folder=O/mode;folder.mkdir(exist_ok=True)
    for name, original, modified in [('IONVRAM.cpp',n,nn),('IONVRAMCHRPHandler.cpp',c,cc)]:
        (folder/name).write_text(original if mode=='baseline' else modified)
        if mode=='candidate':
            (O/(name+'.patch')).write_text(''.join(difflib.unified_diff(original.splitlines(True),modified.splitlines(True),fromfile='a/iokit/Kernel/'+name,tofile='b/iokit/Kernel/'+name)))
for name in ('IONVRAM.cpp','IONVRAMCHRPHandler.cpp'):
    report['original_files'][name]=H(SRC/'iokit/Kernel'/name)
entries=[]
for line in (N/'compile_commands.json').read_text().splitlines():
    line=line.strip().rstrip('$').strip().rstrip(',')
    if line.startswith('{'):entries.append(json.loads(line))
entry=next(e for e in entries if e['file']==str(SRC/'iokit/Kernel/IONVRAM.cpp'))
env=dict(os.environ);env.pop('CCC_OVERRIDE_OPTIONS',None)
for mode in ('baseline','candidate'):
    folder=O/mode;source=folder/'IONVRAM.cpp';obj=folder/'IONVRAM.cpo'
    a=entry['arguments'].copy();needle=str(SRC/'iokit/Kernel/IONVRAM.cpp')
    assert a.count(needle)==1;a[a.index(needle)]=str(source);a[a.index('-o')+1]=str(obj)
    if '-MF' in a:a[a.index('-MF')+1]=str(folder/'IONVRAM.d')
    a += ['-iquote',str(SRC/'iokit/Kernel'),'-Wno-unsafe-buffer-usage']
    with (folder/'build.log').open('w') as f:
        r=subprocess.run(a,cwd=entry['directory'],env=env,stdout=f,stderr=subprocess.STDOUT)
    if r.returncode:
        print((folder/'build.log').read_text()[-9000:]);raise SystemExit(r.returncode)
    report['compiles'][mode]={'source_sha256':H(source),'chrp_sha256':H(folder/'IONVRAMCHRPHandler.cpp'),
                             'object':str(obj),'object_sha256':H(obj),'arguments':a,'directory':entry['directory']}
(O/'compile-review.json').write_text(json.dumps(report,indent=2)+'\n')
print('401 PASS actual target baseline/candidate compiled; imports/ABI/behavior audit pending; NOT integrated or booted')
