Standalone crashprobe464 source qualification; NOT deployed.

Production has no path argument and reads only the fixed CrashReporter directory
proved by the existing BootDiagExport source, plus its optional Retired child.
No old kernel export is linked. Root and child components use no-follow directory
opens. Only exact process-prefix .ips regular files with one link qualify, followed
by independent JSON process-name validation. Current mobile permission/access is
UNKNOWN; root and the existing no-container signing entitlement do not prove it.
No broader entitlement is proposed. There is no launchd job or auto-start here.

Bounds: 512 total directory entries, 32 newest retained candidates, 4 read attempts,
2 MiB/report and 8 MiB/read window; mtime age <=24h with <=300s future tolerance;
16KiB metadata line, JSON depth64 and lexical token budget50000, threads256,
images4096, two selected threads, 24 frames each, 230 output rows/60000 bytes.
Cooperative 10s limit plus15s process alarm. Individual filesystem calls and
Foundation may block; a hard stop can leave a truncated output prefix. No globally
atomic read is claimed. Before/after descriptor and directory-entry identity,
size, mtime and ctime checks detect common races, not all changes. Missing final
end row, unsupported report, incomplete parse/read, permission failure or no
reports means UNKNOWN, never absence of a crash.

Main thread is confirmed only by exact com.apple.main-thread queue; otherwise
thread0 is explicitly a candidate. triggered/faultingThread must agree if both
present. Only two threads are observed. Raw termination description is never
output: fixed event terms are extracted. Exact constant symbol whitelist prevents
an ASCII username from passing a mere character filter; unknown symbols remain
unknown, with numeric imageOffset retained. C++ IPC prefixes map only to fixed
family labels, never raw signature text. Unknown image names are redacted.
No URLs, usernames, device IDs, report/image UUIDs, paths, raw reports, registers,
arbitrary queue strings, or report hashes are emitted. PID is numeric for local
main-thread/termination correlation. ImageOffset does not establish the cause.

Apple primary references:
- https://developer.apple.com/documentation/xcode/interpreting-the-json-format-of-a-crash-report
- https://developer.apple.com/documentation/xcode/addressing-watchdog-terminations
IPS309 uses first-line metadata plus body JSON. Watchdog diagnosis needs the
termination explanation and thread context; the captured final stack need not
identify the work that originally exhausted the time budget.

CI uploads source plus synthetic tests only. It builds/tests Foundation with the
genuine macOS SDK, builds/signs arm64 iOS16 with the genuine iPhoneOS SDK, verifies
signature code pages/special slots and emits declared-library import metadata.
Only text qualification artifacts are retained; NO binary is uploaded/downloaded.
Local actual20H392 cache export/reexport resolution must follow CI success.
Objective-C selectors are not dyld exports: import resolution does not prove
selector availability, permissions, report schema on this phone or successful
runtime. No source-only result authorizes production admission. Main must arrange
a separately qualified delivery process if binary retention is later authorized.
