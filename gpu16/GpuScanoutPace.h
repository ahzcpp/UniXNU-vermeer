// SPDX-License-Identifier: MIT
// Optional 60 Hz cadence for the GPU scanout client (GpuScanoutClient.h), off unless asked for. After a frame's GPU copy was retired
// and its COMPLETE_SWAP returned success, the serving thread waits for the next point of a monotonic 60 Hz grid before it asks for the
// next frame. This file is the arithmetic only: it decides how long to wait and keeps bounded statistics; it never sleeps, holds no
// frame, and knows nothing of Metal or the kernel, so the host test (test_scanout_pace.cpp) compiles it as it is.
//
// Rules:
//  - Only 60 is accepted; 0 is off; any other rate is refused (recorded) and leaves the cadence off.
//  - A frame that is ready before its grid point waits for it; the wait never exceeds one period.
//  - A frame that is ready after its grid point is released at once and the grid restarts from that moment: lateness is never made
//    up, so no two frames are released closer together than one period minus the wake tolerance below.
//  - A wake more than kPaceWakeToleranceNs after the grid point restarts the grid from the wake.
//  - A clock that reads 0 or runs backwards restarts the grid and waits for nothing.
//  - The wait itself (paceWait below) is to the grid point, not for a duration: after every sleep, however it ended, the clock is
//    read again and the remaining time slept again, so an interrupted or early sleep cannot release a frame early. A sleep that
//    reports an error other than an interruption, or a wait that would need more than kPaceSleepLimit sleeps, ends the wait before
//    the grid point: that release is counted in shortReleases (and its cause in sleepErrors / sleepExhausted), never passed off as
//    on the grid.
#pragma once
#include <stdint.h>

namespace gpuscanout {

inline const uint64_t kPacePeriod60Ns = 16666667u;      // 1 s / 60, rounded up: never faster than 60 Hz
inline const uint64_t kPaceWakeToleranceNs = 1000000u;  // a wake this late still belongs to its grid point
inline const uint32_t kPaceSleepLimit = 64;             // sleeps for one grid point, however often they are interrupted

// What a sleep callback reports (paceWait).
enum PaceSleep : int { kPaceSlept = 0, kPaceSleepInterrupted = 1, kPaceSleepFailed = 2 };

struct PaceStats {
    uint32_t refusedHz;        // the rate that was asked for and refused (0: none)
    uint64_t waits;            // frames that waited for their grid point
    uint64_t lateResets;       // frames that were ready after it: released at once, grid restarted
    uint64_t wakeResets;       // waits that woke more than the tolerance late: grid restarted
    uint64_t clockResets;      // clock readings of 0 or going backwards
    uint64_t sleepInterrupts;  // sleeps that ended interrupted, or before their time: the remaining time was slept again
    uint64_t sleepErrors;      // sleeps that failed otherwise: the wait ended there
    uint64_t sleepExhausted;   // waits that still had time left after kPaceSleepLimit sleeps
    uint64_t shortReleases;    // frames released before their grid point (after a sleep error, an exhausted wait or a clock anomaly)
    uint64_t intervals;        // intervals between consecutive releases recorded below
    uint32_t minUs, maxUs;     // of those intervals
    uint64_t sumUs;
    // < 15.0 ms, < 16.0, < 17.4 (on the 60 Hz grid), < 25.0, < 40.0, >= 40.0
    uint64_t buckets[6];
};

class Pace {
public:
    // Returns whether the cadence is on. Resets the grid.
    bool configure(uint32_t hz) {
        period_ = 0; next_ = 0; last_ = 0; released_ = 0; stats_.refusedHz = 0;
        if (hz == 60) period_ = kPacePeriod60Ns;
        else if (hz) stats_.refusedHz = hz;
        return period_ != 0;
    }
    bool on() const { return period_ != 0; }
    // A frame is done at `nowNs` (monotonic): how long to wait before the next one is asked for (0: not at all).
    uint64_t delayNs(uint64_t nowNs) {
        if (!period_) return 0;
        if (!nowNs || nowNs < last_) {
            // This frame is not delayed; the grid restarts here, so the one after it is paced again.
            ++stats_.clockResets; next_ = nowNs ? nowNs + period_ : 0; last_ = nowNs; released_ = 0; deadline_ = 0; return 0;
        }
        last_ = nowNs;
        deadline_ = 0;
        if (!next_) {next_ = nowNs + period_; return 0;}            // the first frame starts the grid
        if (nowNs >= next_) {++stats_.lateResets; next_ = nowNs + period_; return 0;}
        uint64_t wait = next_ - nowNs;
        // More than a period away only after a frame was released before its grid point (a failed sleep): the grid is pulled in to
        // one period from now, so the wait and the grid point agree and no wait is ever longer than a period.
        if (wait > period_) {wait = period_; next_ = nowNs + period_;}
        deadline_ = next_;
        next_ += period_;
        ++stats_.waits;
        return wait;
    }
    // The serving thread goes on to its next WAIT at `nowNs`: after the wait delayNs asked for, or at once when it asked for none.
    void released(uint64_t nowNs) {
        if (!period_) return;
        if (!nowNs || nowNs < last_) {++stats_.clockResets; next_ = 0; last_ = nowNs; released_ = 0; deadline_ = 0; return;}
        last_ = nowNs;
        if (deadline_ && nowNs > deadline_ + kPaceWakeToleranceNs) {++stats_.wakeResets; next_ = nowNs + period_;}
        if (deadline_ && nowNs < deadline_) ++stats_.shortReleases;   // the wait ended early; the grid (next_) stays where it was
        deadline_ = 0;
        if (released_) {
            const uint64_t us64 = (nowNs - released_) / 1000u;
            const uint32_t us = us64 > UINT32_MAX ? UINT32_MAX : (uint32_t)us64;
            if (!stats_.intervals || us < stats_.minUs) stats_.minUs = us;
            if (!stats_.intervals || us > stats_.maxUs) stats_.maxUs = us;
            ++stats_.intervals; stats_.sumUs += us;
            ++stats_.buckets[us < 15000 ? 0 : us < 16000 ? 1 : us < 17400 ? 2 : us < 25000 ? 3 : us < 40000 ? 4 : 5];
        }
        released_ = nowNs;
    }
    const PaceStats &stats() const { return stats_; }
    // For paceWait: the causes of a wait that did not reach its grid point.
    void noteSleepInterrupted() { ++stats_.sleepInterrupts; }
    void noteSleepError() { ++stats_.sleepErrors; }
    void noteSleepExhausted() { ++stats_.sleepExhausted; }
private:
    uint64_t period_ = 0, next_ = 0, last_ = 0, released_ = 0, deadline_ = 0;
    PaceStats stats_ = {};
};

// The whole wait, as the client runs it after a frame's successful completion: asks the cadence, sleeps to the grid point, releases.
// `now()` returns monotonic nanoseconds; `sleep(ns)` sleeps at most `ns` (never more than one period is asked for) and returns a
// PaceSleep. The grid point is a time on `now`'s clock: after every sleep the clock is read again and only the remaining time is
// slept, so the frame is released at the grid point, or early only in the three counted cases (see the rules at the top).
template <typename Now, typename Sleep>
inline void paceWait(Pace &pace, Now now, Sleep sleep)
{
    if (!pace.on()) return;
    const uint64_t began = now();
    const uint64_t wait = pace.delayNs(began);
    if (wait) {
        const uint64_t deadline = began + wait;      // began is non-zero and wait is at most one period: no wrap in practice
        uint64_t at = began;
        for (uint32_t sleeps = 0;; ++sleeps) {
            if (sleeps == kPaceSleepLimit) {pace.noteSleepExhausted(); break;}
            uint64_t remaining = deadline - at;
            if (remaining > kPacePeriod60Ns) remaining = kPacePeriod60Ns;
            const int slept = sleep(remaining);
            if (slept == kPaceSleepFailed) {pace.noteSleepError(); break;}
            const uint64_t after = now();
            if (!after || after < at) break;          // the clock failed: released() below restarts the grid and counts it
            at = after;
            if (at >= deadline) break;
            pace.noteSleepInterrupted();              // interrupted or woken early: sleep the rest
        }
    }
    pace.released(now());
}

}  // namespace gpuscanout
