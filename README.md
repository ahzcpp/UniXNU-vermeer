Owned diagnostic unixnu-wk463 (finite, read-only, root launchd job, -c console mirror). 16 samples at 2 s, hard 80 s alarm,
<= 2400 rows. Roles by executable basename: 1 WebKit.Networking, 2 WebKit.GPU, 3 WebKit.WebContent, 4 MobileSafari.
A: per-socket rows from net.inet.tcp/udp.pcblist_n for role processes (TCP state, cumulative rx/tx bytes over all traffic
classes, unread rcvq / unsent sndq bytes, so_error, remote-port class 443/80/53/other) printed only on new/changed/gone.
B: system stackshots at samples 2,5,8,11,14 (stack_snapshot_with_config, THREAD_WAITINFO); per role task: main thread
state, wait type, wait owner pid/role, special-reply flag, turnstile final inheritor pid/role, CPU ms, bounded frames as
shared-cache / image / unslid-kernel offsets. No IP addresses, local ports, names, URLs or pointers are printed; no signals,
task ports, writes or network traffic. Stackshots briefly pause the system. Struct offsets are tied to xnu-10063.141.1
(pinned public header hashes, regenerated in CI). Runner smoke uses renamed /bin/sleep and /usr/bin/nc copies on the CI
kernel only. No device qualification claimed. No merge.
