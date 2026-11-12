#!/usr/bin/env python3
"""Opt-in bounded telemetry from the iOS16 render client, with no scanout owner."""
from pathlib import Path
import sys,hashlib,json
root=Path(sys.argv[1]);p=root/'drivers/AdrenoMetal/NativeAdrenoMetalPlugin.mm'
old=p.read_text();s=old
anchor='static id clientDevice(unsigned port) {'
assert s.count(anchor)==1
helper=r'''// Diagnostic only. Keep the existing device construction and presentation.
static void startClientDiagnostics(unsigned port,id device) {
    if(!device || !bootArgSet("k70-gpu-client-diag=1"))return;
    if(strcmp(getprogname(),"SBRendServer") && strcmp(getprogname(),"backboardd") &&
       strcmp(getprogname(),"SpringBoard") && strcmp(getprogname(),"Preferences"))return;
    static dispatch_once_t once;
    static dispatch_source_t source;
    dispatch_once(&once,^{
        Gpu *gpu=makeGpu(port);
        if(!gpu || !gpu->openFn){delete gpu;return;}
        source=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_global_queue(QOS_CLASS_UTILITY,0));
        if(!source){delete gpu;return;}
        // The timer retains the sampled device; no image or submission is made.
        id held=device;
        dispatch_source_set_timer(source,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC),10*NSEC_PER_SEC,NSEC_PER_SEC);
        dispatch_source_set_event_handler(source,^{
            (void)[held class];
            char all[8192]={};
            size_t n=native_bridge_metal_telemetry(all,sizeof all);
            if(n>=sizeof all)n=sizeof all-1;
            all[n]=0;
            unsigned row=0;
            auto send=[&](const char *text,size_t size) {
                if(row>=12)return;
                gpu->mark(0xc000u|row++);
                if(size>90)size=90;
                for(size_t at=0;at<size;at+=2) {
                    unsigned a=(unsigned char)text[at]&0x7f;
                    unsigned b=at+1<size?((unsigned char)text[at+1]&0x7f):0;
                    gpu->mark(0x8000u|(a<<7)|b);
                }
            };
            char identity[96];
            int length=snprintf(identity,sizeof identity,"CLIENT %s pid=%d",getprogname(),getpid());
            if(length>0)send(identity,(size_t)length);
            char priming[96];
            length=snprintf(priming,sizeof priming,"PR stage=%u libs=%u/%u made=%u rate=%u",
                native_prime_compiling,native_prime_libraries_done,native_prime_libraries_total,native_prime_made,native_prime_rate);
            if(length>0)send(priming,(size_t)length);
            for(char *line=all;*line && row<12;) {
                char *end=strchr(line,'\n');size_t size=end?(size_t)(end-line):strlen(line);
                if(!strncmp(line,"MEM ",4) || !strncmp(line,"L inp ",6) || !strncmp(line,"TB ",3) ||
                   !strncmp(line,"T compile ",10) || !strncmp(line,"V vkpipe ",9) ||
                   !strncmp(line,"V cache ",8) || !strncmp(line,"J surface ",10) ||
                   !strncmp(line,"RM ",3) || !strncmp(line,"S c",3))send(line,size);
                if(!end)break;line=end+1;
            }
        });
        dispatch_activate(source);
    });
}
'''
s=s.replace(anchor,helper+'\n'+anchor)
anchor='''        if (native_bridge_metal_first_problem(problem, sizeof problem) && problem[0]) clientFailure(port, "problem", problem);
    }
    return device;'''
assert s.count(anchor)==1
s=s.replace(anchor,anchor.replace('    }\n    return device;', '        startClientDiagnostics(port,device);\n    }\n    return device;'))
anchor='''        gpu->mark(0x6803);
        return device;'''
assert s.count(anchor)==1
s=s.replace(anchor,'''        startClientDiagnostics(port,device);
'''+anchor)
# The render server takes deviceForPort's full branch, while ordinary apps
# take clientDevice. Both paths must use the explicit diagnostic switch.
anchor='if (bootArgSet("sheng-diag=1") || bootArgSet("sheng-gpu-trace=1"))'
assert s.count(anchor)==1
s=s.replace(anchor,'if (bootArgSet("sheng-diag=1") || bootArgSet("sheng-gpu-trace=1") || bootArgSet("k70-gpu-client-diag=1"))')
# Volatile Data cannot retain the OS-wide prewarm cache across temporary
# boots. Permit an explicit no-prewarm experiment; normal on-demand shader
# compilation and default behaviour remain intact.
anchor='''        pthread_create(&primer, &attributes, [](void *) -> void * {'''
assert s.count(anchor)==1
s=s.replace(anchor,'''        if(!bootArgSet("k70-gpu-prime=0")) pthread_create(&primer, &attributes, [](void *) -> void * {''')
p.write_text(s)
(root/'build').mkdir(exist_ok=True)
report={'file':str(p.relative_to(root)),'before_sha256':hashlib.sha256(old.encode()).hexdigest(),'after_sha256':hashlib.sha256(s.encode()).hexdigest(),'opt_in':'k70-gpu-client-diag=1','sample_seconds':10,'maximum_rows':12,'scanout_owner_added':False,'rendering_policy_changed_by_default':False,'shader_priming_disable_opt_in':'k70-gpu-prime=0','render_server_full_branch_sampled':True,'device_tested':False}
(root/'build/ios16-client-diagnostics.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
