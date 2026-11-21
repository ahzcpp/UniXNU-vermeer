Owned diagnostic unixnu-wk469 (finite, read-only, root launchd job, -c console mirror). Successor of wk463: rows keep
the "WK463 " prefix and all 463 fields; 469 appends fields and adds row kinds. 16 samples at 2 s, hard 80 s alarm,
<= 4000 rows. Roles by executable basename: 1 WebKit.Networking, 2 WebKit.GPU, 3 WebKit.WebContent, 4 MobileSafari,
5 AppStore.
A: per-socket rows from net.inet.tcp/udp.pcblist_n for role processes (TCP state, cumulative rx/tx bytes, unread rcvq /
unsent sndq, so_error, remote-port class 443/80/53/other) printed only on new/changed/gone, with per-sample completeness
accounting (short records, incomplete groups, trailer).
B: system stackshots at samples 2,5,8,11,14 (stack_snapshot_with_config, THREAD_WAITINFO). Every role task: main thread
with thread id and in-snapshot ordinal, wait type, owner classified by wait type (thread id / pid / port name / kernel
marker), owner thread resolved to pid+ordinal, turnstile final inheritor (thread id -> pid+ordinal, or pid), CPU ms,
bounded frames as shared-cache / image / unslid-kernel offsets, all image UUIDs (i0..i7). Roles 2, 4, 5: all other threads,
at most 24 per process, priority to threads referenced as owner/inheritor, with explicit truncation counts.
No IP addresses, local ports, names, URLs or pointers are printed (thread ids are kernel sequence numbers); no signals,
task ports, writes or network traffic. Stackshots briefly pause the system. Struct offsets are tied to xnu-10063.141.1
(pinned public header hashes, regenerated in CI). Runner smoke uses renamed /bin/sleep and /usr/bin/nc copies and a
runner-only lock/dispatch helper on the CI kernel only. No device qualification claimed. No merge.
