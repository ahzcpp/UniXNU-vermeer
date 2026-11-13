#!/usr/bin/env python3
"""GpuScanoutClient: an explicit, default-off scaled read-back diagnostic and an optional, default-off 60 Hz cadence.

Applies to drivers/AdrenoMetal of the pinned UniXNU tree, after patch-gpu-scanout-scale.py (which touches other files: the
presenter and the AIR). Usage: patch-gpu-scanout-verify.py <tree root>. Source only: no firmware, addresses, identifiers or binaries.

With neither boot argument the client does strictly less than before: no read-back at all (the old one compared the first four
frames' top 400 rows at unscaled coordinates with the source's alpha, which means nothing once the copy is scaled).

  k70-scanout-verify=1   diagnostic mode, not for performance runs: full-frame read-back of at most 4 non-blank frames in at most
                         12 tries per boot (the frames numbered 1, 2, 4, ... 2048), after the GPU retired the copy and before
                         COMPLETE_SWAP. It never changes the frame's result or what is sent to the kernel.
  k70-scanout-pace=60    after a frame's copy was retired AND its COMPLETE_SWAP returned success, wait for the next point of a
                         monotonic 60 Hz grid before the next WAIT_SWAP (GpuScanoutPace.h). Nothing is held across the wait.

Files:
  GpuScanoutVerify.h, GpuScanoutPace.h   new, no Metal: the host tests compile them unchanged
  GpuScanoutClient.h                     Options; Stats gains fields at its end; verify(); the cadence wait
  NativeMetalBridgePhase.h               reads the two boot arguments; report lines "PV ..." and "PP ..."; and, a change of default
                                         behaviour on its own: the "PF" progress line goes out at the first frame, the first failure
                                         and every 120th frame (it was every fifth; each character is one IOServiceOpen)
"""
from pathlib import Path
import hashlib, json, shutil, sys

root = Path(sys.argv[1]).resolve()
base = root / 'drivers/AdrenoMetal'
here = Path(__file__).resolve().parent
BASELINE = {
    'GpuScanoutClient.h': '1ac37cd05b90e1bc14be1aa28e155489f78c77dbaa2fa0d5bca9b6d1b457049c',
    'NativeMetalBridgePhase.h': '796cf647f46a99c6b7744ae82e8f9ff8394b849dabb9e66bc32cdff66036ec46',
}
def sha(data): return hashlib.sha256(data).hexdigest()
def replace(s, old, new, count=1):
    assert s.count(old) == count, (old[:90], s.count(old), count)
    return s.replace(old, new)

report = {}
for name, want in BASELINE.items():
    have = sha((base / name).read_bytes())
    assert have == want, 'unexpected baseline for %s: %s' % (name, have)

# 1. The two helpers, byte for byte the ones the host tests ran.
for name in ('GpuScanoutVerify.h', 'GpuScanoutPace.h'):
    shutil.copyfile(here / name, base / name)
    report['drivers/AdrenoMetal/' + name] = dict(before=None, after=sha((here / name).read_bytes()))

# 2. The client.
p = base / 'GpuScanoutClient.h'; old = p.read_bytes(); s = old.decode()
s = replace(s, '#include <unistd.h>\n#include "GpuScanoutPresenter.h"\n',
               '#include <unistd.h>\n#include <errno.h>\n#include <string.h>\n#include <time.h>\n#include <sys/sysctl.h>\n#include "GpuScanoutPresenter.h"\n#include "GpuScanoutVerify.h"\n#include "GpuScanoutPace.h"\n')
s = replace(s, '''    uint32_t lastMs;    // the GPU copy's time for the last frame
    uint64_t totalMs;   // and for all frames (the mean is totalMs / (frames + failures))
};''', '''    uint32_t lastMs;    // the GPU copy's time for the last frame
    uint64_t totalMs;   // and for all frames (the mean is totalMs / (frames + failures))
    // The read-back diagnostic (Options.verify, GpuScanoutVerify.h); all zero without it. `verified` above counts the frames that
    // were compared whole and were not an agreeing blank frame; `mismatchRows` their rows with a wrong B, G or R.
    uint64_t verifyAttempts;   // read-backs started (at most 12 a boot)
    uint64_t uniformFrames;    // compared, agreed, and every pixel had one colour: proves little, so not counted as verified
    uint64_t alphaRows;        // rows of the verified frames with an alpha that is not 0xff
    uint64_t alphaBlitFrames;  // verified frames whose every such alpha was the source's (a blit made the copy, not the kernel)
    uint64_t verifyRefused;    // read-backs refused before any load (a buffer the comparison may not read)
    uint64_t verifyRows;       // scanout rows compared in all read-backs (the coverage: whole frames only)
    uint64_t verifyUs;         // time spent in all read-backs, during which the frame's completion was held back
    uint32_t verifyLastUs;     // and in the last one
    uint32_t verifyStatus;     // the last read-back's gpuscanout::VerifyStatus
    uint32_t firstRow, lastRow, firstColumn;   // of the last verified frame that differed
    PaceStats pace;            // the 60 Hz cadence (Options.paceHz, GpuScanoutPace.h); all zero without it
};

// What the client does beyond serving frames. All off by default: a client that is not configured reads nothing back and never waits.
struct Options {
    bool verify;        // the read-back diagnostic: adds a full-frame CPU read of device memory to a few frames; not for timing runs
    uint32_t paceHz;    // 60: the cadence; 0: none; anything else is refused and recorded in Stats.pace.refusedHz
};''')
s = replace(s, '''    bool open(id<MTLDevice> device, const Transport &transport, Stats *stats) {
        transport_ = transport;
        stats_ = stats;
''', '''    // Before open(). Returns whether the cadence that was asked for is on (true when none was asked for).
    bool configure(const Options &options) {
        options_ = options;
        const bool paced = pace_.configure(options.paceHz);
        return paced || !options.paceHz;
    }
    bool open(id<MTLDevice> device, const Transport &transport, Stats *stats) {
        transport_ = transport;
        stats_ = stats;
        stats_->pace = pace_.stats();
''')
s = replace(s, '''        if (r == gpupresent::kOk && swap.format == NGP_BGRX8888 && stats_->verified < 4) verify(swap);
        note(0x6731);   // the GPU copy returned
        const uint64_t copiedUs = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000;
''', '''        note(0x6731);   // the GPU copy returned
        const uint64_t copiedUs = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000;
        // The read-back diagnostic runs here and nowhere else: the GPU has retired the copy (present() waited for it), and the swap is
        // not completed yet, so the display driver still holds the offered surface (with sheng-early-retire=0; an early retire lets the
        // compositor reuse it, and a difference then proves nothing). It changes neither `r` nor what is sent below.
        if (options_.verify && r == gpupresent::kOk && swap.format == NGP_BGRX8888 && verifyDue()) verify(swap);
''')
s = replace(s, '''        if (completed) {++stats_->failures; stats_->error = completed; return false;}
        if (r == gpupresent::kOk) ++stats_->frames; else ++stats_->failures;
        return true;
''', '''        if (completed) {++stats_->failures; stats_->error = completed; return false;}
        if (r == gpupresent::kOk) ++stats_->frames; else ++stats_->failures;
        // The cadence, only for a frame whose copy the GPU retired and whose completion the kernel accepted. Nothing of that frame is
        // referenced from here on (`swap` is not used again, no surface or mapping is kept), and no kernel call is in progress while
        // the thread sleeps; the next frame is asked for only after it.
        if (r == gpupresent::kOk && pace_.on()) paceWait();
        return true;
''')
start = s.index('    // Reads the scanout back with aligned 32-bit loads')
end = s.index('    void note(uint32_t value)')
s = s[:start] + '''    // Whether this served frame is read back: the frames numbered 1, 2, 4, ... 2048 (twelve tries a boot), until four were counted.
    bool verifyDue() const {
        const uint64_t number = stats_->frames + 1;   // this frame; frames counts the ones already completed
        return stats_->verified < 4 && stats_->verifyAttempts < 12 && !(number & (number - 1));
    }
    // Reads the whole scanout back and compares every row with the surface the GPU copied, pixel for pixel as the scaled kernel maps
    // them (GpuScanoutVerify.h). Aligned 32-bit volatile loads only (the framebuffer is device memory; wider loads can fault); both
    // buffers are validated against the lengths the kernel reported before the first load. Evidence only; nothing is written.
    void verify(const NGPSwap &swap) {
        ++stats_->verifyAttempts;
        const uint64_t beganNs = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        // The surface as the kernel offered it: its own byte count, not the page-rounded one the import uses.
        const VerifyView source = {(const volatile uint32_t *)(uintptr_t)swap.address, swap.bytes, swap.stride, swap.width, swap.height};
        const VerifyView shown = {(const volatile uint32_t *)scanout_.base, (uint64_t)scanout_.bytes, (uint64_t)scanout_.stride,
                                  scanout_.width, scanout_.height};
        const VerifyResult result = verifyScaledOpaque(source, shown, 0);
        const uint64_t endedNs = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        const uint64_t us = endedNs > beganNs ? (endedNs - beganNs) / 1000 : 0;
        stats_->verifyLastUs = us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
        stats_->verifyUs += us;
        stats_->verifyStatus = result.status;
        if (result.status != kVerifyCompared) {++stats_->verifyRefused; return;}
        stats_->verifyRows += result.rows;
        const bool differs = result.colourRows || result.alphaRows;
        if (result.uniform && !differs) {++stats_->uniformFrames; return;}
        ++stats_->verified;
        stats_->mismatchRows += result.colourRows;
        stats_->alphaRows += result.alphaRows;
        if (result.alphaIsSource) ++stats_->alphaBlitFrames;
        if (differs) {stats_->firstRow = result.firstRow; stats_->lastRow = result.lastRow; stats_->firstColumn = result.firstColumn;}
    }
    // Waits for the cadence's next grid point (gpuscanout::paceWait, the code the host test runs with a fake clock): the clock is
    // read again after every sleep and the rest slept, so an interrupted nanosleep does not release the frame early.
    void paceWait() {
        gpuscanout::paceWait(pace_, [] { return (uint64_t)clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }, [](uint64_t ns) -> int {
            const struct timespec request = {(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)};
            if (nanosleep(&request, nullptr) == 0) return kPaceSlept;
            return errno == EINTR ? kPaceSleepInterrupted : kPaceSleepFailed;
        });
        stats_->pace = pace_.stats();
    }
''' + s[end:]
s = replace(s, '''    gpupresent::Scanout scanout_ = {};
    Stats *stats_ = nullptr;
''', '''    gpupresent::Scanout scanout_ = {};
    Stats *stats_ = nullptr;
    Options options_ = {};
    Pace pace_;
''')
p.write_bytes(s.encode())
report['drivers/AdrenoMetal/GpuScanoutClient.h'] = dict(before=sha(old), after=sha(s.encode()))

# 3. The boot arguments and the report lines.
p = base / 'NativeMetalBridgePhase.h'; old = p.read_bytes(); s = old.decode()
s = replace(s, '''static void serveScanout(Gpu &gpu, id device) {
''', '''// Whether the kernel's boot arguments contain `key` (a whole "name=value" word). The plugin's sandbox already reads kern.bootargs.
static bool scanoutBootArg(const char *key) {
    char args[1100] = {};
    size_t size = sizeof args - 1;
    if (sysctlbyname("kern.bootargs", args, &size, nullptr, 0) != 0) return false;
    const size_t length = strlen(key);
    for (const char *at = args; (at = strstr(at, key)); at += length) {
        const bool starts = at == args || at[-1] == ' ', ends = at[length] == 0 || at[length] == ' ';
        if (starts && ends) return true;
    }
    return false;
}
static void serveScanout(Gpu &gpu, id device) {
''')
s = replace(s, '''        gpuscanout::Client client;
        const bool opened = client.open((id<MTLDevice>)device, transport, &st);
''', '''        gpuscanout::Client client;
        // Both off unless the boot arguments ask: k70-scanout-verify=1 (read-back diagnostic), k70-scanout-pace=60 (60 Hz cadence).
        const gpuscanout::Options options = {scanoutBootArg("k70-scanout-verify=1"), scanoutBootArg("k70-scanout-pace=60") ? 60u : 0u};
        client.configure(options);
        const bool opened = client.open((id<MTLDevice>)device, transport, &st);
''')
s = replace(s, '''            uint64_t reportedFrames = 0, reportedFailures = 0;
            while (client.serveOne(500)) {
''', '''            uint64_t reportedFrames = 0, reportedFailures = 0, reportedVerify = 0, reportedPace = 0;
            while (client.serveOne(500)) {
                if (st.verifyAttempts != reportedVerify) {
                    // "PV <verified>/<tries> u<agreeing blank frames> c<colour rows> a<alpha rows><b: a blit's alpha> r<first>-<last row>
                    //  x<first column> s<last status> t<last read-back ms>": the read-back diagnostic, only with k70-scanout-verify=1.
                    reportedVerify = st.verifyAttempts;
                    snprintf(line, sizeof line, "PV %llu/%llu u%llu c%llu a%llu%s r%u-%u x%u s%u t%u", (unsigned long long)st.verified,
                             (unsigned long long)st.verifyAttempts, (unsigned long long)st.uniformFrames,
                             (unsigned long long)st.mismatchRows, (unsigned long long)st.alphaRows, st.alphaBlitFrames ? "b" : "",
                             st.firstRow, st.lastRow, st.firstColumn, st.verifyStatus, st.verifyLastUs / 1000);
                    streamText7(gpu, line);
                }
                if (options.paceHz && st.pace.intervals - reportedPace >= 600) {
                    // "PP <intervals> l<late> k<wake+clock resets> s<short releases> i<min>/<mean>/<max us> b<on-grid>/<slower>": the
                    // cadence, only with k70-scanout-pace=60; every 600 paced frames. on-grid: 16.0 to 17.4 ms; slower: 25 ms and
                    // more. s counts frames released before their grid point (a failed or exhausted sleep): 0 when the cadence held.
                    reportedPace = st.pace.intervals;
                    snprintf(line, sizeof line, "PP %llu l%llu k%llu s%llu i%u/%llu/%u b%llu/%llu", (unsigned long long)st.pace.intervals,
                             (unsigned long long)st.pace.lateResets, (unsigned long long)(st.pace.wakeResets + st.pace.clockResets),
                             (unsigned long long)st.pace.shortReleases, st.pace.minUs,
                             (unsigned long long)(st.pace.sumUs / st.pace.intervals), st.pace.maxUs,
                             (unsigned long long)st.pace.buckets[2], (unsigned long long)(st.pace.buckets[4] + st.pace.buckets[5]));
                    streamText7(gpu, line);
                }
''')
# The PF progress line went out at the first frame and then every fifth: each character of it is one IOServiceOpen (streamText7), so a
# 60 fps stream made hundreds of opens a second. Now: the first frame, the first failure, and every 120th frame after.
s = replace(s, 'const bool frame = st.frames && (st.frames == 1 || st.frames - reportedFrames >= 5);',
               'const bool frame = st.frames && (st.frames == 1 || st.frames - reportedFrames >= 120);')
s = replace(s, 'at the first frame, every fifth after', 'at the first frame, every 120th after')
p.write_bytes(s.encode())
report['drivers/AdrenoMetal/NativeMetalBridgePhase.h'] = dict(before=sha(old), after=sha(s.encode()))

(root / 'build').mkdir(exist_ok=True)
(root / 'build/ios16-scanout-verify.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
