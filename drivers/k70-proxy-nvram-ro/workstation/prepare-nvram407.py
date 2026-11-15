"""Same407 kernel/resources/NECP/RAM; only explicit RO selector 1 vs0."""
from pathlib import Path
import hashlib,json,shutil
P=Path(__file__).resolve().parent;D=Path('/mnt/d/K70-UniXNU-data');B=D/'wifi-variant394';base=json.loads((B/'package-command.json').read_text());v=json.loads((B/'verified-inputs.json').read_text())
H=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert H(v['addon'])==v['addon_sha256'] and H(v['trust'])==v['trust_sha256']
assert base[base.index('--necp16-abi')+1]==base[base.index('--ram-zero-holes')+1]=='1'
assert json.loads((D/'kernel407-work/startup-audit.json').read_text())['normal_constructor_in_native_and_prelinked']
for number,mode in [(407,1),(408,0)]:
    O=D/f'wifi-variant{number}';O.mkdir(exist_ok=False);shutil.copy2(B/'verified-inputs.json',O/'verified-inputs.json')
    for q in B.glob('ios16-*.json'):shutil.copy2(q,O/q.name)
    args=base.copy();args[args.index('--number')+1]=str(number);args[args.index('--kernel-number')+1]='407';args[args.index('--addon-inputs')+1]=str(O/'verified-inputs.json');args+=['--nvram-proxy-ro',str(mode)]
    (O/'package-command.json').write_text(json.dumps(args,indent=2)+'\n')
    (O/'main-review.json').write_text(json.dumps(dict(kernel=407,base_resources=393,resources_identical394=True,addon_sha256=v['addon_sha256'],trust_sha256=v['trust_sha256'],NECP16_requested=1,RAM_zero_holes_requested=1,nvram_proxy_ro_requested=mode,persistent=False,hardware_tested=False,repack_current_clock_before_future_boot=True),indent=2)+'\n')
    print(number,'prepared explicit RO selector',mode,'same407kernel+393resources+NECP/RAM1; no phone')
