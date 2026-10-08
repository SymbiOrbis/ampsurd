#include "ampsurd/ParallelRunner.h"

#include <algorithm>
#include <chrono>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #include <immintrin.h>
  #define AMPSURD_X86 1
#endif

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <avrt.h>
#else
  #include <pthread.h>
  #include <sched.h>
#endif

namespace ampsurd
{

namespace
{
inline void cpuRelax() noexcept
{
#if AMPSURD_X86
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

void makeRealtime() noexcept
{
#if AMPSURD_X86
    // denormals: flush to zero / treat as zero (as JUCE's ScopedNoDenormals does on the audio thread)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
#if defined(_WIN32)
    DWORD taskIndex = 0;
    AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex); // same scheduling class as ASIO threads
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
    sched_param p {};
    p.sched_priority = std::max(1, sched_get_priority_max(SCHED_FIFO) - 2);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &p); // ignored if not permitted
#endif
}

constexpr std::uint64_t kIndexMask = 0xffffffffull;
} // namespace

ParallelRunner::~ParallelRunner() { stop(); }

int ParallelRunner::recommendedWorkers(int maxUseful) noexcept
{
    // Only on computers with at least 4 logical cores: with fewer, a worker that the system pauses
    // (other programs need the same cores) would make the audio thread wait (measured on 2 cores).
    const int cores = (int) std::thread::hardware_concurrency();
    if (cores < 4) return 0;
    return std::clamp(cores - 1, 0, std::max(0, maxUseful));
}

void ParallelRunner::start(int workers)
{
    stop();
    quit.store(false);
    for (int i = 0; i < workers; ++i)
        threads.emplace_back([this] { workerLoop(); });
}

void ParallelRunner::stop()
{
    if (threads.empty()) return;
    quit.store(true);
    work.fetch_add(1ull << 32); // new generation wakes everyone
    work.notify_all();
    for (auto& t : threads) t.join();
    threads.clear();
}

bool ParallelRunner::claimAndRun(std::uint32_t gen) noexcept
{
    std::uint64_t v = work.load(std::memory_order_acquire);
    for (;;)
    {
        if ((std::uint32_t) (v >> 32) != gen) return false; // a newer block: nothing of ours left
        if (work.compare_exchange_weak(v, v + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            break;
    }
    const int index = (int) (v & kIndexMask);
    if (index >= numJobsNow.load(std::memory_order_relaxed)) return false;
    fnNow.load(std::memory_order_relaxed)(ctxNow.load(std::memory_order_relaxed), index);
    jobsDone.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

void ParallelRunner::run(int numJobs, JobFn fn, void* context) noexcept
{
    if (numJobs <= 0) return;
    if (threads.empty() || numJobs == 1)
    {
        for (int i = 0; i < numJobs; ++i) fn(context, i);
        return;
    }
    numJobsNow.store(numJobs, std::memory_order_relaxed);
    fnNow.store(fn, std::memory_order_relaxed);
    ctxNow.store(context, std::memory_order_relaxed);
    jobsDone.store(0, std::memory_order_relaxed);
    const std::uint64_t old = work.load(std::memory_order_relaxed);
    const auto gen = (std::uint32_t) ((old >> 32) + 1);
    work.store((std::uint64_t) gen << 32, std::memory_order_seq_cst); // publish: index 0 of a new block
    if (sleeping.load(std::memory_order_seq_cst) > 0) work.notify_all();

    while (claimAndRun(gen)) {}                       // the audio thread works too
    while (jobsDone.load(std::memory_order_acquire) < numJobs) cpuRelax(); // jobs already started by workers
}

void ParallelRunner::workerLoop()
{
    makeRealtime();
    std::uint32_t seen = (std::uint32_t) (work.load() >> 32);
    for (;;)
    {
        // wait for a new block: spin ~0.3 ms (blocks follow each other quickly), then sleep
        std::uint64_t v = work.load(std::memory_order_acquire);
        const auto spinUntil = std::chrono::steady_clock::now() + std::chrono::microseconds(300);
        int checks = 0;
        while ((std::uint32_t) (v >> 32) == seen && !quit.load(std::memory_order_relaxed))
        {
            cpuRelax();
            v = work.load(std::memory_order_acquire);
            if ((++checks & 63) == 0 && std::chrono::steady_clock::now() > spinUntil)
            {
                sleeping.fetch_add(1, std::memory_order_seq_cst);
                v = work.load(std::memory_order_seq_cst);
                if ((std::uint32_t) (v >> 32) == seen && !quit.load())
                    work.wait(v, std::memory_order_acquire); // returns when `work` changes
                sleeping.fetch_sub(1, std::memory_order_acq_rel);
                v = work.load(std::memory_order_acquire);
            }
        }
        if (quit.load()) return;
        seen = (std::uint32_t) (v >> 32);
        while (claimAndRun(seen)) {}
    }
}

} // namespace ampsurd
