#!/usr/bin/env python3
"""Adapt the measured 20H392 Metal base and retain creation failure details."""
from pathlib import Path
import sys,json,hashlib
root=Path(sys.argv[1]);changes=[]
p=root/'drivers/AdrenoMetal/NativeBridgeMetalFrontend.mm';old=p.read_text()
anchor='if(!base || class_getInstanceSize(base)!=536)'
assert old.count(anchor)==1
new=old.replace(anchor,'if(!base || class_getInstanceSize(base)!=528)')
p.write_text(new)
changes.append(dict(file=str(p.relative_to(root)),before=hashlib.sha256(old.encode()).hexdigest(),after=hashlib.sha256(new.encode()).hexdigest(),
    reason='20H392 _MTLDevice class_ro_t instanceSize=528, class name and Metal export verified from original cache; dynamic subclass adds no ivars'))
p=root/'drivers/AdrenoMetal/NativeAdrenoMetalPlugin.mm';old=p.read_text();new=old
anchor='        char candidates[6][512] = {};'
assert new.count(anchor)==1
new=new.replace(anchor,anchor+'\n        char lastOpenException[128] = {};')
anchor='''            } @catch (NSException *exception) {
                device = nil;
            }'''
assert new.count(anchor)==1
new=new.replace(anchor,'''            } @catch (NSException *exception) {
                device = nil;
                const char *name=exception.name.UTF8String;
                const char *reason=exception.reason.UTF8String;
                snprintf(lastOpenException,sizeof lastOpenException,"%.24s %.80s",name?name:"?",reason?reason:"?");
            }''')
anchor='            snprintf(text, sizeof text, "PUB no device: %.40s", nmb_last_error());'
assert new.count(anchor)==1
new=new.replace(anchor,'''            if(lastOpenException[0])snprintf(text,sizeof text,"PUB exception %.48s",lastOpenException);
            else snprintf(text, sizeof text, "PUB no device: %.40s", nmb_last_error());''')
p.write_text(new)
changes.append(dict(file=str(p.relative_to(root)),before=hashlib.sha256(old.encode()).hexdigest(),after=hashlib.sha256(new.encode()).hexdigest(),reason='Retain Objective-C exception name/reason instead of reporting an empty Vulkan failure'))
(root/'build/ios16-metal-device-abi.json').write_text(json.dumps(dict(target='20H392',expected_base_size=528,changes=changes),indent=2)+'\n')
print('Applied exact iOS16 Metal base-size guard and creation diagnostics')
