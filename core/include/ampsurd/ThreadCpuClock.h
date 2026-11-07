#pragma once

// ThreadCpuClock (owner 2026-11-07): how long the CALLING thread really ran, not how much time passed on the clock.
// A dropout block that took 44 ms on the clock, of which AMPSURD ran 1 ms, means the computer paused AMPSURD
// (another program, power saving, a driver); if it ran all 44 ms, AMPSURD itself was too slow.
//
// Windows: QueryThreadCycleTime against the CPU's time-stamp counter (both in the same cycles; x64 only).
// macOS / Linux: clock_gettime(CLOCK_THREAD_CPUTIME_ID). Cheap enough to call twice per audio block; no allocation.

namespace ampsurd
{
struct ThreadCpuClock
{
    struct Mark
    {
        double cpu = 0.0; // Windows: cycles this thread ran; else: nanoseconds this thread ran
        double ref = 0.0; // Windows: time-stamp counter; else unused
        bool ok = false;
    };
    static Mark now() noexcept;
    // milliseconds the thread ran between a and b (wallMs: the clock time between them); -1 = not known here
    static double ranMs(const Mark& a, const Mark& b, double wallMs) noexcept;
};
} // namespace ampsurd
