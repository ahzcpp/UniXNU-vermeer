#!/usr/bin/env python3
"""Keep optional PP/PV observations separate from PF progress.

At frame601, PP interval600 and PF progress both publish on report7. PF
immediately overwrites PP before the host's periodic observer can see it.
Route optional diagnostics to the existing report8 stream, and report the
parsed options once. No render, wait, completion or verifier logic changes.
"""
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]).resolve()
base=root/'drivers/AdrenoMetal'
p=base/'NativeMetalBridgePhase.h'
old=p.read_bytes()
before=hashlib.sha256(old).hexdigest()
assert before=='7c93c7d320bf6d8af725eca413de5a4f60acde4a7883cb1771db04ffc5f5dbda'
s=old.decode()
anchor='''        client.configure(options);
        const bool opened = client.open((id<MTLDevice>)device, transport, &st);'''
assert s.count(anchor)==1
s=s.replace(anchor,'''        client.configure(options);
        snprintf(line, sizeof line, "PC pace=%u verify=%u", options.paceHz, unsigned(options.verify));
        streamText(gpu, line); // report8; PF progress owns report7
        const bool opened = client.open((id<MTLDevice>)device, transport, &st);''')
for final in ['st.verifyLastUs / 1000);',
              '(unsigned long long)(st.pace.buckets[4] + st.pace.buckets[5]));']:
    anchor=final+'\n                    streamText7(gpu, line);'
    assert s.count(anchor)==1
    s=s.replace(anchor,final+'\n                    streamText(gpu, line);')
new=s.encode();p.write_bytes(new)
report={'before':before,'after':hashlib.sha256(new).hexdigest(),
        'option_report':'PC pace=<0|60> verify=<0|1>',
        'optional_diagnostic_channel':8,'progress_channel':7,
        'render_wait_complete_logic_changed':False,'hardware_tested':False,
        'unchanged_helpers':{n:hashlib.sha256((base/n).read_bytes()).hexdigest()
                             for n in ['GpuScanoutClient.h','GpuScanoutPace.h','GpuScanoutVerify.h']}}
(root/'build/ios16-scanout-diag-channel.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
