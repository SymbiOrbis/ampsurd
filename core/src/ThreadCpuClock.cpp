#include "ampsurd/ThreadCpuClock.h"

#include <algorithm>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #if defined(_M_X64) || defined(_M_IX86)
    #include <intrin.h>
    #define AMPSURD_HAS_TSC 1
  #endif
#else
  #include <time.h>
#endif

namespace ampsurd
{
ThreadCpuClock::Mark ThreadCpuClock::now() noexcept
{
    Mark m;
#if defined(_WIN32)
  #if defined(AMPSURD_HAS_TSC)
    ULONG64 cycles = 0;
    if (QueryThreadCycleTime(GetCurrentThread(), &cycles))
    {
        m.cpu = (double) cycles;
        m.ref = (double) __rdtsc();
        m.ok = true;
    }
  #endif
#else
    timespec ts {};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
    {
        m.cpu = (double) ts.tv_sec * 1.0e9 + (double) ts.tv_nsec;
        m.ok = true;
    }
#endif
    return m;
}

double ThreadCpuClock::ranMs(const Mark& a, const Mark& b, double wallMs) noexcept
{
    if (!a.ok || !b.ok || wallMs <= 0.0) return -1.0;
#if defined(_WIN32)
    const double dRef = b.ref - a.ref;
    if (dRef <= 0.0) return -1.0;
    return std::clamp(wallMs * (b.cpu - a.cpu) / dRef, 0.0, wallMs);
#else
    return std::clamp((b.cpu - a.cpu) * 1.0e-6, 0.0, wallMs);
#endif
}
} // namespace ampsurd
