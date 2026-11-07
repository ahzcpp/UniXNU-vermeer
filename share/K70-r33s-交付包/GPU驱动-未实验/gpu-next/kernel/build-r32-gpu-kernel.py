#!/usr/bin/env python3
"""r32-gpu kernel (derived by gpu-next/scripts/make-gpu-kernel.py from build-r32-kernel.py): r31 + host touch processing from GT9916 difference frames (derived by make-r32-kernel.py).

Derived from build-r8-kernel.py by make-r9-kernel.py; r8 notes follow.


Changes against r6, nothing else:
 1. observer: boot_progress_prepare_scanout() stops routine painting (console stays swallowed so
    the stock video console cannot draw over iOS); a critical exit repaints the trace.
 2. bsd_init.c: after IOKitBSDInit() publishes IOBSD, call k70_display_start() and print its result.
 3. link: force-load display-candidate libK70GopDisplay.candidate.a (NativeLegacyDisplay + K70 provider).
Never contacts a phone.
"""
import hashlib,json,os,shlex,shutil,struct,subprocess,sys,time,tomllib
from pathlib import Path
GPU_OUT=Path(__file__).resolve().parent;HERE=GPU_OUT.parent.parent/'usb-next'
GPU_TREE=Path('/root/k70-gpu-work/tree')
GPU_ARCHIVES=[GPU_TREE/'build/adreno740/libAdreno740.a',GPU_TREE/'build/k70-gpu/libK70Gpu.a']
ROOT=Path('/root/k70-clean-20261104');WORK=ROOT/'storage-candidate-20261104'
TREE=WORK/'out/vermeer/ipados-17.7.11-21H461/vermeer/tree';NATIVE=TREE/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'
SRC=TREE/'.cache/xnu-work';R6=WORK/'r6-usb-20261105';OUT=Path('/root/k70-gpu-work/kernel-r32-gpu');COPY=OUT/'xnu-work-r32';HIDDIR=WORK/'hid-objects-20261106';LOGO=ROOT/'applelogo-21H461';R8=WORK/'r8-display-20261105';OBJ=OUT/'obj'
CORE=ROOT/'UniXNU';DEVICE=WORK/'device';OBSERVER_SRC=HERE.parent/'storage-next/observer-next'
DISPLAY=ROOT/'display-candidate-20261104/libK70GopDisplay.candidate.a'
DISPLAY_SHA='4fa31ce273dcc2b469ec492282624cff379cbfc4e69bdd6cb3e8ac028b1d0b97'
FILES=['osfmk/arm/arm_init.c','pexpert/arm/pe_serial.c','pexpert/arm/pe_kprintf.c','osfmk/console/serial_console.c','osfmk/kern/startup.c','bsd/kern/bsd_init.c','bsd/kern/kern_exec.c']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
report={'schema':'k70-r32-thp-kernel/1','status':'running','steps':[],'device_tested':False}
def save():(GPU_OUT/'r32-gpu-kernel-result.json').write_text(json.dumps(report,indent=2)+'\n')
def run(label,args,cwd,environment):
    args=list(map(str,args));log=OUT/(label+'.log');start=time.time();print('Running '+label,flush=True)
    with log.open('w') as f:r=subprocess.run(args,cwd=cwd,env=environment,stdout=f,stderr=subprocess.STDOUT)
    report['steps'].append({'name':label,'exit_code':r.returncode,'seconds':round(time.time()-start,2),'log':str(log)})
    if r.returncode:report['status']='failed';save();print(log.read_text(errors='replace')[-6000:]);raise SystemExit(r.returncode)
    save();print('PASS '+label,flush=True)
assert sha(DISPLAY)==DISPLAY_SHA,'display candidate archive changed'
assert json.loads((HERE/'r6-kernel-result.json').read_text())['status']=='compiled_prelinked_probed_storage_verified_not_device_tested'
OUT.mkdir(exist_ok=True);OBJ.mkdir(exist_ok=True)

# 1. Source copy: r6's patched tree (USB tee hooks) plus the display start call.
if not COPY.exists():
    shutil.copytree(R8/'xnu-work-r8',COPY,symlinks=True)
    p=COPY/'bsd/kern/bsd_init.c';t=p.read_text()
    old='\t\tprintf("K70 display start: %d\\n", k70_display_error);\n\t}\n'
    assert t.count(old)==1
    t=t.replace(old,'\t\tprintf("K70 display start: %d\\n", k70_display_error);\n'
        '\t\textern volatile int k70_display_start_error;\n\t\tk70_display_start_error = k70_display_error;\n'
        '\t\t/* r9: owned AppleKeyStore (offline-key-events=1 sheng-aks=1); unblocks liveactivitiesd/chronod. */\n'
        '\t\textern void native_keystore_probe(void);\n\t\tnative_keystore_probe();\n\t\t/* GPU build: Adreno 740 power-up, ZAP, CP and Metal publication (sheng-gpu-power=N). */\n\t\textern void sheng_gpu_power_probe(void);\n\t\tsheng_gpu_power_probe();\n\t\textern void native_hid_input_probe(void);\n\t\tnative_hid_input_probe();\n\t\textern void k70_touch_start(void);\n\t\tk70_touch_start();\n\t}\n')
    p.write_text(t)
    ke=COPY/'bsd/kern/kern_exec.c';kt=ke.read_text()
    anchor='\tboot_progress_stage("temporary watchdog launch policy ready");\n'
    assert kt.count(anchor)==1 and 'k70_migration_baseline' not in kt
    kt=kt.replace(anchor,anchor+'\t{\n\t\textern int k70_migration_baseline(void);\n'
        '\t\tint k70_migration_error = k70_migration_baseline();\n'
        '\t\tprintf("K70 migration baseline: %d\\n", k70_migration_error);\n'
        '\t\tboot_progress_stage(k70_migration_error ? "migration baseline FAILED" : "migration baseline ready");\n\t}\n'
        '\t{\n\t\textern int k70_accessibility_baseline(void);\n'
        '\t\tint k70_ax_error = k70_accessibility_baseline();\n'
        '\t\tprintf("K70 accessibility baseline: %d\\n", k70_ax_error);\n\t}\n'
        '\t{\n\t\textern int k70_skip_setup(void);\n'
        '\t\tint k70_skip_error = k70_skip_setup();\n'
        '\t\tprintf("K70 skip setup: %d\\n", k70_skip_error);\n'
        '\t\tboot_progress_stage(k70_skip_error ? "skip setup FAILED" : "skip setup ready");\n\t}\n')
    kt+='''
/*
 * r21: on the fresh Data seed, DataMigration runs the upgrade plugins and never finishes here, so
 * SpringBoard waits for the system-shell check-in and backboardd for DataMigration. Record 21H461 as
 * already migrated (the same preference ios-vermeer-lab writes for 20H392). Opt-in through
 * k70-migration-baseline=1; an existing preference is never replaced.
 */
int k70_migration_baseline(void);
int
k70_migration_baseline(void)
{
	static const char path[] = "/private/var/mobile/Library/Preferences/com.apple.migration.plist";
	static const char plist[] = "<?xml version=\\"1.0\\"?><plist version=\\"1.0\\"><dict>"
	    "<key>LastSystemVersion</key><string>21H461</string>"
	    "<key>DMUserDataDisposition</key><dict>"
	    "<key>SyntheticDidUpgrade</key><false/>"
	    "<key>buildVersion</key><string>21H461</string>"
	    "</dict></dict></plist>\\n";
	vfs_context_t ctx = vfs_context_current();
	vnode_t vp = NULLVP;
	struct vnode_attr va;
	int residual = 0, error, close_error;
	unsigned int enabled = 0;

	if (!PE_parse_boot_argn("k70-migration-baseline", &enabled, sizeof(enabled)) || enabled != 1) {
		return 0;
	}
	error = vnode_open(path, FWRITE | O_CREAT | O_EXCL | O_NOFOLLOW, 0600, VNODE_LOOKUP_NOFOLLOW, &vp, ctx);
	if (error == EEXIST) {
		return 0;
	}
	if (error) {
		return error;
	}
	error = vn_rdwr(UIO_WRITE, vp, (caddr_t)(uintptr_t)plist, (int)(sizeof(plist) - 1), 0,
	    UIO_SYSSPACE, IO_SYNC, vfs_context_ucred(ctx), &residual, current_proc());
	if (!error && residual != 0) {
		error = EIO;
	}
	if (!error) {
		VATTR_INIT(&va);
		VATTR_SET(&va, va_uid, 501);
		VATTR_SET(&va, va_gid, 501);
		error = vnode_setattr(vp, &va, ctx);
	}
	close_error = vnode_close(vp, FWRITE, ctx);
	return error ? error : close_error;
}

/*
 * r27: no GPU, so CoreAnimation's backdrop (blur) layers render black. Reduce Transparency makes UIKit and
 * SpringBoard use solid materials instead, and Reduce Motion trims animations for the CPU renderer.
 * Opt-in through k70-reduce-transparency=1; an existing com.apple.Accessibility preference is never replaced.
 */
int k70_accessibility_baseline(void);
int
k70_accessibility_baseline(void)
{
	static const char path[] = "/private/var/mobile/Library/Preferences/com.apple.Accessibility.plist";
	static const char plist[] = "<?xml version=\\"1.0\\"?><plist version=\\"1.0\\"><dict>"
	    "<key>EnhancedBackgroundContrastEnabled</key><true/>"
	    "<key>ReduceMotionEnabled</key><true/>"
	    "</dict></plist>\\n";
	vfs_context_t ctx = vfs_context_current();
	vnode_t vp = NULLVP;
	struct vnode_attr va;
	int residual = 0, error, close_error;
	unsigned int enabled = 0;

	if (!PE_parse_boot_argn("k70-reduce-transparency", &enabled, sizeof(enabled)) || enabled != 1) {
		return 0;
	}
	error = vnode_open(path, FWRITE | O_CREAT | O_EXCL | O_NOFOLLOW, 0600, VNODE_LOOKUP_NOFOLLOW, &vp, ctx);
	if (error == EEXIST) {
		return EEXIST;
	}
	if (error) {
		return error;
	}
	error = vn_rdwr(UIO_WRITE, vp, (caddr_t)(uintptr_t)plist, (int)(sizeof(plist) - 1), 0,
	    UIO_SYSSPACE, IO_SYNC, vfs_context_ucred(ctx), &residual, current_proc());
	if (!error && residual != 0) {
		error = EIO;
	}
	if (!error) {
		VATTR_INIT(&va);
		VATTR_SET(&va, va_uid, 501);
		VATTR_SET(&va, va_gid, 501);
		error = vnode_setattr(vp, &va, ctx);
	}
	close_error = vnode_close(vp, FWRITE, ctx);
	return error ? error : close_error;
}

/*
 * r28: boot to the lock screen instead of Setup, in Simplified Chinese. The values are UniXNU's
 * scripts/seed-data-skip-setup.py (ios-sweet, proven on hardware) with --no-auto-dim, plus the language.
 * Opt-in through k70-skip-setup=1. Each file is replaced, since the kernel cannot merge property lists.
 */
static int
k70_replace_pref(const char *name, const char *text, int length)
{
	char path[160];
	vfs_context_t ctx = vfs_context_current();
	vnode_t vp = NULLVP;
	struct vnode_attr va;
	int residual = 0, error, close_error;

	snprintf(path, sizeof(path), "/private/var/mobile/Library/Preferences/%s.plist", name);
	error = vnode_open(path, FWRITE | O_CREAT | O_NOFOLLOW, 0600, VNODE_LOOKUP_NOFOLLOW, &vp, ctx);
	if (error) {
		return error;
	}
	VATTR_INIT(&va);
	VATTR_SET(&va, va_data_size, 0);
	error = vnode_setattr(vp, &va, ctx);
	if (!error) {
		error = vn_rdwr(UIO_WRITE, vp, (caddr_t)(uintptr_t)text, length, 0,
		    UIO_SYSSPACE, IO_SYNC, vfs_context_ucred(ctx), &residual, current_proc());
	}
	if (!error && residual != 0) {
		error = EIO;
	}
	if (!error) {
		VATTR_INIT(&va);
		VATTR_SET(&va, va_uid, 501);
		VATTR_SET(&va, va_gid, 501);
		VATTR_SET(&va, va_mode, 0600);
		error = vnode_setattr(vp, &va, ctx);
	}
	close_error = vnode_close(vp, FWRITE, ctx);
	return error ? error : close_error;
}
#define K70_PLIST(body) "<?xml version=\\"1.0\\"?><plist version=\\"1.0\\"><dict>" body "</dict></plist>\\n"
int k70_skip_setup(void);
int
k70_skip_setup(void)
{
	static const char buddy[] = K70_PLIST(
	    "<key>SetupDone</key><true/><key>SetupFinishedAllSteps</key><true/>"
	    "<key>SetupVersion</key><integer>11</integer><key>daemonizeNeedsToRun</key><false/>");
	static const char mini[] = K70_PLIST(
	    "<key>ShouldRunDiagnosticsMiniBuddy</key><false/><key>ShouldRunHSA2MiniBuddy</key><false/>"
	    "<key>ShouldRunKeychainSyncMiniBuddy</key><false/><key>ShouldRunMesaMiniBuddy</key><false/>"
	    "<key>ShouldRunPaymentMiniBuddy</key><false/><key>ShouldRunStandardMiniBuddy</key><false/>"
	    "<key>SilentICDPUpgrade</key><false/>");
	static const char mc[] = K70_PLIST(
	    "<key>LockdownActivationIndicatesCloudConfigurationAvailable</key><false/>");
	static const char sb[] = K70_PLIST(
	    "<key>BKNoWatchdogs</key><true/>"
	    "<key>FBLaunchWatchdogExceptions</key><dict><key>com.apple.purplebuddy</key><real>100</real></dict>"
	    "<key>MCForceShowCloudConfigurationUI</key><false/>"
	    "<key>SBDisableAutoDim</key><true/><key>SBDontDimOrLockOnAC</key><true/><key>SBDisableIdleSleep</key><true/>");
	static const char global[] = K70_PLIST(
	    "<key>AppleLanguages</key><array><string>zh-Hans-CN</string></array>"
	    "<key>AppleLocale</key><string>zh_CN</string>");
	static const struct { const char *name, *text; int length; } prefs[] = {
		{"com.apple.purplebuddy", buddy, sizeof(buddy) - 1},
		{"com.apple.purplebuddy.notbackedup", mini, sizeof(mini) - 1},
		{"com.apple.managedconfiguration.notbackedup", mc, sizeof(mc) - 1},
		{"com.apple.springboard", sb, sizeof(sb) - 1},
		{".GlobalPreferences", global, sizeof(global) - 1},
	};
	unsigned int enabled = 0;
	int first_error = 0;

	if (!PE_parse_boot_argn("k70-skip-setup", &enabled, sizeof(enabled)) || enabled != 1) {
		return 0;
	}
	for (unsigned int i = 0; i < sizeof(prefs) / sizeof(prefs[0]); i++) {
		int error = k70_replace_pref(prefs[i].name, prefs[i].text, prefs[i].length);
		printf("K70 skip setup: %s -> %d\\n", prefs[i].name, error);
		if (error && !first_error) {
			first_error = error;
		}
	}
	return first_error;
}
'''
    ke.write_text(kt)
    u=COPY/'pexpert/arm/k70_xnu_usb.c';ut=u.read_text()
    assert 'k70_xnu_usb_status_line' not in ut
    u.write_text(ut+'''
/* r14: on-screen USB state. part 0: switches and registers; part 1: the last message. */
const char *k70_xnu_usb_status_line(unsigned part);
const char *
k70_xnu_usb_status_line(unsigned part)
{
    static char line[2][100];
    const char *msg = m.Status.LastMessage;
    if (part) {
        if (!strncmp(msg, "K70 XNU USB: ", 13)) msg += 13;
        snprintf(line[1], sizeof(line[1]), "USB: %s", msg[0] ? msg : "(no message)");
        return line[1];
    }
    snprintf(line[0], sizeof(line[0]), "USB en=%d run=%d fail=%d st=%d gcc=%08x gctl=%08x dsts=%08x",
        debug_enabled, (int)m.Status.Running, (int)m.Status.Failed, (int)m.Status.LastStatus,
        (unsigned)m.Status.GccPower, (unsigned)m.Status.Gctl, (unsigned)m.Status.DstsBefore);
    return line[0];
}
''')
report['patched_sources']={rel:sha(COPY/rel) for rel in FILES};save()

os.environ['UNIXNU_TOOLCHAIN']=str(WORK/'toolchain-view');sys.path.insert(0,str(CORE/'scripts'))
from unixnu.cli import parser
from unixnu.config import Context
from unixnu import kernel
ctx=Context(parser().parse_args(['build','kernel','-D',str(DEVICE),'--out',str(WORK/'out'),'--kernelcache',str(ROOT/'out/ipsw/21H461/kernelcache.decompressed'),'--jobs','4']))
env=dict(os.environ,**kernel.linux_env(ctx));clean_env=dict(env);clean_env.pop('CCC_OVERRIDE_OPTIONS',None)
clean_env['PATH']='/usr/lib/llvm-18/bin:'+clean_env['PATH'];clean_env['UNIXNU_UEFI']=str(ROOT/'uefi');clean_env['UNIXNU_UEFI_ROOT']=str(ROOT/'uefi')

# 2. Observer with the scanout yield, same recipe as build-observer.py.
obs_dir=OUT/'observer';obs_dir.mkdir(exist_ok=True)
for name in ('EventRing.h','TrapFrame.h'):shutil.copyfile(OBSERVER_SRC/name,obs_dir/name)
s=(OBSERVER_SRC/'BootProgressMin.c').read_text()
subs=[
 ('static void\nbp_refresh(void)\n{\n\tif (!g_screen.bound) {\n\t\treturn;\n\t}\n',
  '/* r8: set once a validated iOS frame owns scanout; the console stays swallowed. */\nstatic volatile int g_yielded;\n\n'
  'static void\nbp_refresh(void)\n{\n\tif (!g_screen.bound || __atomic_load_n(&g_yielded, __ATOMIC_ACQUIRE)) {\n\t\treturn;\n\t}\n'),
 ('int\nboot_progress_active(void)\n',
  'int boot_progress_prepare_scanout(void);\n'
  '/* Called by the legacy display before each CPU scanout copy. */\nint\nboot_progress_prepare_scanout(void)\n{\n'
  '\t__atomic_store_n(&g_yielded, 1, __ATOMIC_RELEASE);\n\treturn 1;\n}\n\nint\nboot_progress_active(void)\n'),
]
for old,new in subs:
    assert s.count(old)==1,old[:60];s=s.replace(old,new)
# A critical exit must stay visible even after the handoff.
crit='boot_progress_critical_exit(int pid, char *prefix, unsigned long length)\n{\n'
assert s.count(crit)==1;s=s.replace(crit,crit+'\t__atomic_store_n(&g_yielded, 0, __ATOMIC_RELEASE);\n')
splash=(HERE/'r10-splash.inc.c').read_text()+(HERE/'r24-strip.inc.c').read_text()
assert (LOGO/'k70_applelogo.h').is_file(),'run fetch-applelogo.py and make-logo-header.py first'
for old,new in [
 ('} g_screen;\n','} g_screen;\n\n/* r9: white-apple splash instead of the log page (k70-splash=1). */\nstatic unsigned g_splash;\n#define BP_SPLASH_STATUS 4u\n'),
 ('\tfor (row = 0; row < g_screen.rows && row < BP_MAX_ROWS; row++) {\n\t\tchar *shown',
  '\tfor (row = g_splash ? g_screen.rows - BP_SPLASH_STATUS : 0; row < g_screen.rows && row < BP_MAX_ROWS; row++) {\n\t\tchar *shown'),
 ('/* r8: set once a validated iOS frame owns scanout',splash+'\n/* r8: set once a validated iOS frame owns scanout'),
 ('\tif (!g_screen.bound || __atomic_load_n(&g_yielded, __ATOMIC_ACQUIRE)) {\n\t\treturn;\n\t}\n',
  '\tif (!g_screen.bound) {\n\t\treturn;\n\t}\n\tif (__atomic_load_n(&g_yielded, __ATOMIC_ACQUIRE)) {\n\t\tif (g_splash) {\n\t\t\tbp_strip_refresh();\n\t\t}\n\t\treturn;\n\t}\n\tif (g_splash) {\n\t\tbp_splash_refresh();\n\t\treturn;\n\t}\n'),
 ('\tg_screen.painted = 0;\n\tg_screen.bound = 1;\n',
  '\t{\n\t\tunsigned splash_arg = 0;\n\n\t\tif (PE_parse_boot_argn("k70-splash", &splash_arg, sizeof(splash_arg)) && splash_arg == 1) {\n\t\t\tg_splash = 1;\n\t\t}\n\t}\n\tg_screen.painted = 0;\n\tg_screen.bound = 1;\n'),
 ('\t__atomic_store_n(&g_yielded, 0, __ATOMIC_RELEASE);\n','\t__atomic_store_n(&g_yielded, 0, __ATOMIC_RELEASE);\n\tbp_splash_leave();\n'),
]:
    assert s.count(old)==1,old[:70];s=s.replace(old,new)
for old,new in [
 ('static void\nbp_record_known(','struct bp_event;\nstatic void bp_crash_note(const struct bp_event *event);\n\nstatic void\nbp_record_known('),
 ('    (void)bp_event_push(&g_events, &event);\n','    if (kind == \'X\' && !event.name[0] && pid > 0) {\n        proc_name(pid, event.name, BP_EVENT_NAME);\n        event.name[BP_EVENT_NAME - 1u] = 0;\n    }\n    bp_crash_note(&event);\n    (void)bp_event_push(&g_events, &event);\n'),
 ('struct bp_event;\n','struct bp_event;\nvoid proc_name(int pid, char *buf, int size);\n')]:
    assert s.count(old)==1,old[:60];s=s.replace(old,new)
(obs_dir/'BootProgressMin.c').write_text(s)
record=json.loads((NATIVE/'osfmk/DEVELOPMENT/startup.o.json').read_text().strip().rstrip(','));args=record['arguments'];command=[];i=0
obs_obj=obs_dir/'BootProgressMin.o'
while i<len(args):
    a=args[i]
    if a==record['file']:command.append(str(obs_dir/'BootProgressMin.c'))
    elif a in ('-o','-MF','-MJ','-MT','-MQ'):
        if a=='-o':command+=['-o',str(obs_obj)]
        i+=1
    elif a=='-Xclang' and args[i+1].startswith('-W'):i+=1
    elif a=='-D' and args[i+1].startswith('XNU_BOOT_PROGRESS='):i+=1
    elif not a.startswith(('-W','-DXNU_BOOT_PROGRESS=')) and a not in ('-MD','-MP','-MMD'):command.append(a)
    i+=1
command+=['-DXNU_BOOT_PROGRESS=1','-mgeneral-regs-only','-Wall','-Wextra','-Werror','-I',str(TREE/'.cache/xnu-work/osfmk'),'-I',str(TREE/'kits/vermeer'),'-I',str(obs_dir),'-I',str(LOGO)]
run('compile-observer-r32',command,record['directory'],env)
obs_archive=obs_dir/'libBootProgress.a';obs_archive.unlink(missing_ok=True)
run('archive-observer-r32',['xcrun','ar','rcs',obs_archive,obs_obj],OUT,env)

# 3. Recompile the six hooked XNU objects with their recorded arguments.
entries=[]
for line in (NATIVE/'compile_commands.json').read_text().splitlines():
    line=line.strip().rstrip('$').strip().rstrip(',')
    if line.startswith('{'):entries.append(json.loads(line))
swapped={}
for rel in FILES:
    [e]=[e for e in entries if e['file']==str(SRC/rel)]
    a=[x.replace(str(SRC),str(COPY)) for x in e['arguments']]
    i=a.index('-o');original=Path(e['directory'])/a[i+1]
    new=OBJ/(rel.replace('/','_')[:-2]+'.o');a[i+1]=str(new)
    if '-MF' in a:j=a.index('-MF');a[j+1]=str(new.with_suffix('.d'))
    run('compile-'+new.stem,a,e['directory'],env)
    swapped[str(original)]=str(new)

# 4. Link: frozen recipe, six objects swapped, r8 observer, display archive force-loaded.
flags=shlex.split((NATIVE/'.LDFLAGS').read_text().replace('\0','').strip());libcc=flags.pop();assert libcc.endswith('/libcc_kext.a')
old_bytes=(NATIVE/'mach.development.bcm2837.unstripped.noctf').read_bytes();off=32;source_version=None
for _ in range(struct.unpack_from('<I',old_bytes,16)[0]):
    cmd,size=struct.unpack_from('<II',old_bytes,off)
    if cmd==0x2a:source_version=struct.unpack_from('<Q',old_bytes,off+8)[0]
    off+=size
flags+=['-Wl,-source_version,'+'.'.join(str((source_version>>s)&m) for s,m in ((40,0xffffff),(30,1023),(20,1023),(10,1023),(0,1023)))]
lines=(NATIVE/'link.filelist').read_text().splitlines()
assert all(lines.count(k)==1 for k in swapped)
filelist=OUT/'link.filelist';filelist.write_text('\n'.join(swapped.get(l,l) for l in lines)+'\n')
archives=shlex.split((NATIVE/'mach.development.bcm2837.link/mach.development.bcm2837.linkarchives').read_text())
old_archive='-Wl,-force_load,'+str(TREE/'build/boot-progress/libBootProgress.a');assert archives.count(old_archive)==1
selected=[('-Wl,-force_load,'+str(obs_archive)) if v==old_archive else v for v in archives]+['-Wl,-force_load,'+str(DISPLAY)]
explicit=[NATIVE/(n+'.o') for n in ('lastkerneldataconst','lastkernelconstructor','nonlto','version')]
# The candidate provider also defines five counters that NativeLegacyDisplay.cpp owns (it was never
# link-tested). Rebuild only the provider with them declared extern, same recorded command.
CAND=DISPLAY.parent;disp_dir=OUT/'display';disp_dir.mkdir(exist_ok=True)
ps=(CAND/'source/K70DisplayProvider.cpp').read_text()
for old in ('volatile uint32_t native_legacy_surfaces[14],native_legacy_direct[14];\n',
            'volatile uint64_t native_legacy_early_retires,native_legacy_early_failures,native_legacy_swap_timeouts;\n'):
    assert ps.count(old)==1,old;ps=ps.replace(old,'extern '+old)
for old,new in (('    kprintf("K70 display: firmware mapping %u width %u height %u stride %lu\\n",valid,width,height,video.v_rowBytes);\n','    if(valid && video.v_depth==32){k70_fb=(uint32_t *)map->getVirtualAddress();k70_fb_stride=(uint32_t)(video.v_rowBytes/4);k70_fb_w=width;k70_fb_h=height;}\n    kprintf("K70 display: firmware mapping %u width %u height %u stride %lu\\n",valid,width,height,video.v_rowBytes);\n'),('    if(status==kIOReturnSuccess)k70_display_bytes+=bytes;\n','    if(status==kIOReturnSuccess){k70_display_bytes+=bytes;k70_fill_backdrop();}\n'),('bool native_legacy_gop_mapping_observed(','\nstatic uint32_t *k70_fb,k70_fb_stride,k70_fb_w,k70_fb_h,k70_fill_state;\nstatic void k70_fill_backdrop(void) {\n    if(!k70_fill_state){unsigned v=0;k70_fill_state=(PE_parse_boot_argn("k70-fill-backdrop",&v,sizeof(v)) && v==1)?1:2;}\n    if(k70_fill_state!=1 || !k70_fb)return;\n    const uint32_t fill=0x001c2a4au;   /* x8r8g8b8: deep blue, a stand-in for the blurred wallpaper */\n    for(uint32_t y=0;y<k70_fb_h;y++) {\n        uint32_t *row=k70_fb+(uint64_t)y*k70_fb_stride,x=0,e=k70_fb_w;\n        while(x<e && !(row[x]&0x00ffffffu))row[x++]=fill;\n        while(e>x && !(row[e-1]&0x00ffffffu))row[--e]=fill;\n    }\n}\n'+'bool native_legacy_gop_mapping_observed(')):
    assert ps.count(old)==1,old;ps=ps.replace(old,new)
(disp_dir/'K70DisplayProvider.cpp').write_text(ps)
pcmd=json.loads((CAND/'K70DisplayProvider-command.json').read_text())
pcmd=[str(disp_dir/'K70DisplayProvider.cpp') if a==str(CAND/'source/K70DisplayProvider.cpp') else
      str(disp_dir/'K70DisplayProvider.o') if a==str(CAND/'K70DisplayProvider.o') else a for a in pcmd]
assert str(disp_dir/'K70DisplayProvider.o') in pcmd and str(disp_dir/'K70DisplayProvider.cpp') in pcmd
pcmd+=['-I',str(CAND/'source')]
run('compile-k70-display-provider',pcmd,NATIVE/'iokit/DEVELOPMENT',env)
display_archive=disp_dir/'libK70GopDisplay.r32.a';display_archive.unlink(missing_ok=True)
aks_objects=[]
for label,source in (('native-keystore-events',CORE/'drivers/AppleKeyStoreState/NativeKeyStoreEvents.cpp'),
                     ('keystore-probe',DEVICE/'package/KeyStoreProbe.cpp')):
    obj=disp_dir/(source.stem+'.o')
    cmd=[str(source) if a==str(disp_dir/'K70DisplayProvider.cpp') else str(obj) if a==str(disp_dir/'K70DisplayProvider.o') else a for a in pcmd]
    run('compile-'+label,cmd,NATIVE/'iokit/DEVELOPMENT',env)
    if label=='native-keystore-events':
        # Same treatment as NativeLegacyDisplay.late.o: export the static initializer and drop it
        # from __mod_init_func, so KeyStoreProbe registers the metaclass inside preModLoad/postModLoad.
        sys.path.insert(0,str(HERE.parent/'display-next/reference'))
        from coachz_late_init import stage_initializer
        late=obj.with_suffix('.late.o');late.write_bytes(stage_initializer(obj.read_bytes(),'__GLOBAL__sub_I_NativeKeyStoreEvents.cpp'))
        obj=late
    aks_objects.append(obj)
touch_obj=disp_dir/'k70_touch.o'
[be]=[e for e in entries if e['file']==str(SRC/'bsd/kern/bsd_init.c')]
tcmd=[str(HERE/'touch/k70_touch.c') if a==str(SRC/'bsd/kern/bsd_init.c') else a for a in be['arguments']]
ti=tcmd.index('-o');tcmd[ti+1]=str(touch_obj)
if '-MF' in tcmd:tj=tcmd.index('-MF');tcmd[tj+1]=str(touch_obj.with_suffix('.d'))
assert str(HERE/'touch/k70_touch.c') in tcmd
tcmd+=['-I',str(HERE/'touch'),'-I',str(HERE.parent/'device-source/package')]
run('compile-k70-touch',tcmd,be['directory'],env)
run('archive-k70-display',['xcrun','ar','rcs',display_archive,CAND/'NativeLegacyDisplay.late.o',CAND/'original-bindings.o',disp_dir/'K70DisplayProvider.o',*aks_objects,*[HIDDIR/n for n in ('NativeHidInput.late.o','hid-original-bindings.o','HidInputProbe.o','k70-input-glue.o')],touch_obj],OUT,env)
selected[-1]='-Wl,-force_load,'+str(display_archive)
# The display driver weak-imports the Adreno fence/trace API but calls it with direct BL, so the
# symbols cannot be address 0 in this static link. With no GPU: tracing is off, and every fence
# reports submitted == retired, i.e. CPU-written frames are complete at once (a stub returning
# false would make every frame wait on a fence that never retires).
absent_src=OUT/'gpu-absent.c'
absent_src.write_text('''/* r8: CPU-only display; no Adreno driver is linked. */
typedef unsigned int u32;
int native_gpu_trace_enabled(void) { return 0; }
void native_gpu_trace_record(u32 event, u32 pid, const unsigned long long *values, u32 count)
{ (void)event; (void)pid; (void)values; (void)count; }
_Bool native_adreno_fences(u32 *submitted, u32 *retired) { *submitted = 0; *retired = 0; return 1; }
_Bool native_adreno_fences_detail(u32 *submitted, u32 *retired, u32 *detail)
{ (void)detail; *submitted = 0; *retired = 0; return 1; }
''')
absent_obj=OUT/'gpu-absent.o'
run('compile-gpu-absent',[ROOT/'toolchain/llvm/bin/clang-24','--target=arm64-apple-ios17.0','-mkernel','-fno-builtin','-O2','-c',absent_src,'-o',absent_obj],OUT,env)
for archive in GPU_ARCHIVES:
    assert archive.is_file(),archive
selected+=['-Wl,-force_load,'+str(a) for a in GPU_ARCHIVES]  # GPU build: real fence/trace API, no gpu-absent.o
native=OUT/'mach.r32-thp.unstripped.noctf';native.unlink(missing_ok=True)
run('link-r32-native',flags+['-filelist',str(filelist),*map(str,explicit),'-o',str(native),libcc,*selected],NATIVE,env)

# 5. Prelink, entry/panic probe, storage hard gate.
spec=tomllib.loads((DEVICE/'board.toml').read_text())['kernel']['prelink'];cache=ROOT/'out/ipsw/21H461/kernelcache.decompressed'
assert sha(cache)==spec['kernelcache_sha256']
prelinked=OUT/'kernel-prelinked.macho'
for stale in (prelinked,prelinked.with_suffix('.json'),prelinked.with_suffix('.map.json'),OUT/'kernel-payload.macho',OUT/'kernel-payload.json'):
    stale.unlink(missing_ok=True)
command=[sys.executable,TREE/spec['tool'],'--kernel',native,'--prelinked',cache,'--slide='+str(spec['slide'])]
for root in spec['roots']:command+=['--root',root]
if spec.get('early_security_init'):command+=['--early-security-init',spec['early_security_init']]
command+=spec.get('extra_args',[])+['--output',prelinked]
oldmap=(TREE.parent/'kernel/kernel.macho').with_suffix('.map.json')
if oldmap.is_file():shutil.copyfile(oldmap,prelinked.with_suffix('.map.json'))
run('prelink-r32',command,TREE,env)
plan=json.loads(prelinked.with_suffix('.json').read_text())
if spec.get('regression_policy'):kernel.check_prelink_regressions(plan,json.loads((TREE/spec['regression_policy']).read_text()))
old_plan=json.loads((TREE.parent/'kernel/kernel.macho').with_suffix('.json').read_text())
assert plan['components']==old_plan['components'] and plan['original_executable_sha256']==old_plan['original_executable_sha256']
payload=OUT/'kernel-payload.macho';shutil.copyfile(prelinked,payload)
run('patch-entry-and-panic',[sys.executable,TREE/'scripts/patch-xnu-entry-probe.py',payload,'--panic-screen','--no-reset'],TREE,clean_env)
derived=dict(plan);derived.update({'output':str(payload),'prelink_candidate_sha256':sha(prelinked),'candidate_sha256':sha(payload),'post_prelink_derivation':{'tool':str(TREE/'scripts/patch-xnu-entry-probe.py'),'tool_sha256':sha(TREE/'scripts/patch-xnu-entry-probe.py'),'arguments':['--panic-screen','--no-reset']}})
payload.with_suffix('.json').write_text(json.dumps(derived,indent=2)+'\n')
(GPU_OUT/'r32-gpu-final-storage-verification.json').unlink(missing_ok=True)  # this script's own earlier proof
run('final-storage-hard-gate',[sys.executable,HERE.parent/'storage-next/verify-final-storage.py','--kernel',payload,'--output',GPU_OUT/'r32-gpu-final-storage-verification.json'],TREE,clean_env)
nm=subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-nm','-u',str(payload)],text=True).strip();assert not nm,nm
syms=subprocess.check_output(['/usr/lib/llvm-18/bin/llvm-nm',str(payload)],text=True)
for s in ('_k70_touch_start','_k70_touch_status_line','_k70_touch_sink_ready','_native_hid_input_probe','_native_hid_input_allocate','_native_touch_set_sink','_k70_display_start','_boot_progress_prepare_scanout','_native_legacy_display_allocate','_k70_xnu_early','_bp_record_known','_native_keystore_probe','_native_keyevents_allocate','_k70_display_start_error','_boot_progress_ext','_sheng_gpu_power_probe','_native_adreno_firmware_image','_native_adreno_sqe_firmware_image','_native_adreno_zap_image'):
    assert any(l.endswith(' '+s) and l.split()[1] not in 'Uu' for l in syms.splitlines()),s
for s in ('_native_adreno_fences','_native_adreno_fences_detail','_native_gpu_trace_enabled','_native_gpu_trace_record'):
    assert any(l.endswith(' '+s) and l.split()[1] in 'TtSs' for l in syms.splitlines()),('GPU fence/trace API missing',s)
report.update({'status':'compiled_prelinked_probed_storage_verified_not_device_tested','display_archive_sha256':DISPLAY_SHA,
 'native':{'path':str(native),'sha256':sha(native)},'kernel':{'path':str(payload),'sha256':sha(payload),'bytes':payload.stat().st_size},
 'all_undefined_resolved':True,'apple_components_match_r5_plan':True});save()
print(json.dumps(report['kernel'],indent=2))
