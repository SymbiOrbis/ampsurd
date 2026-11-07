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
#if defined(__APPLE__)
  #include <pthread/qos.h>
#endif

namespace ampsurd
{

namespace
{
thread_local ParallelRunner* tlsCurrent = nullptr;
thread_local bool tlsAudioThread = false;

struct CurrentScope
{
    explicit CurrentScope(ParallelRunner* r) noexcept : prev(tlsCurrent) { tlsCurrent = r; }
    ~CurrentScope() { tlsCurrent = prev; }
    ParallelRunner* prev;
};

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
#elif defined(__aarch64__)
    // ARM (Apple Silicon): FPCR.FZ flushes denormals to zero
    std::uint64_t fpcr = 0;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    asm volatile("msr fpcr, %0" : : "r"(fpcr | (1ull << 24)));
#endif
#if defined(_WIN32)
    DWORD taskIndex = 0;
    AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex); // same scheduling class as ASIO threads
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#elif defined(__APPLE__)
    // highest normal priority; keeps the worker on the performance cores of Apple Silicon
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#else
    sched_param p {};
    p.sched_priority = std::max(1, sched_get_priority_max(SCHED_FIFO) - 2);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &p); // ignored if not permitted
#endif
}

constexpr std::uint64_t kIndexMask = 0xffffffffull;

// Wakes sleeping workers. Without std::atomic::wait (macOS before 11) the workers poll instead
// (sleepUntilChanged), so there is nothing to wake.
inline void wakeAll(std::atomic<std::uint64_t>& a) noexcept
{
#if AMPSURD_NO_ATOMIC_WAIT
    (void) a;
#else
    a.notify_all();
#endif
}

inline void sleepUntilChanged(std::atomic<std::uint64_t>& a, std::uint64_t old) noexcept
{
#if AMPSURD_NO_ATOMIC_WAIT
    // checks every 0.2 ms; only while multi-core is on and no audio is running
    while (a.load(std::memory_order_acquire) == old)
        std::this_thread::sleep_for(std::chrono::microseconds(200));
#else
    a.wait(old, std::memory_order_acquire); // returns when `a` changes
#endif
}
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
    wakeAll(work);
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
    {
        CurrentScope cs(this);
        fnNow.load(std::memory_order_relaxed)(ctxNow.load(std::memory_order_relaxed), index);
    }
    jobsDone.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

void ParallelRunner::run(int numJobs, JobFn fn, void* context, bool sideWork) noexcept
{
    if (numJobs <= 0) return;
    tlsAudioThread = true;
    if (threads.empty() || (numJobs == 1 && !sideWork))
    {
        CurrentScope cs(threads.empty() ? nullptr : this);
        for (int i = 0; i < numJobs; ++i) fn(context, i);
        return;
    }
    sideOpen.store(sideWork, std::memory_order_relaxed);
    numJobsNow.store(numJobs, std::memory_order_relaxed);
    fnNow.store(fn, std::memory_order_relaxed);
    ctxNow.store(context, std::memory_order_relaxed);
    jobsDone.store(0, std::memory_order_relaxed);
    const std::uint64_t old = work.load(std::memory_order_relaxed);
    const auto gen = (std::uint32_t) ((old >> 32) + 1);
    work.store((std::uint64_t) gen << 32, std::memory_order_seq_cst); // publish: index 0 of a new block
    if (sleeping.load(std::memory_order_seq_cst) > 0) wakeAll(work);

    while (claimAndRun(gen)) {}                       // the audio thread works too
    if (jobsDone.load(std::memory_order_acquire) < numJobs) // jobs already started by workers
    {
        // meanwhile help with their side work; only the time spent just waiting counts as waiting
        auto t = std::chrono::steady_clock::now();
        while (jobsDone.load(std::memory_order_acquire) < numJobs)
        {
            if (takeSide())
            {
                t = std::chrono::steady_clock::now();
                continue;
            }
            cpuRelax();
            const auto now = std::chrono::steady_clock::now();
            waitedMs += std::chrono::duration<double, std::milli>(now - t).count();
            t = now;
        }
    }
}

ParallelRunner* ParallelRunner::current() noexcept { return tlsCurrent; }

int ParallelRunner::offer(JobFn fn, void* context, int index) noexcept
{
    if (threads.empty()) return -1;
    for (int h = 0; h < kSide; ++h)
    {
        int expected = 0;
        if (!side[(size_t) h].state.compare_exchange_strong(expected, 1, std::memory_order_acquire)) continue;
        auto& sj = side[(size_t) h];
        sj.fn = fn;
        sj.context = context;
        sj.index = index;
        sj.state.store(2, std::memory_order_release); // offered
        return h;
    }
    return -1;
}

bool ParallelRunner::takeSide() noexcept
{
    for (auto& sj : side)
    {
        if (sj.state.load(std::memory_order_relaxed) != 2) continue;
        int expected = 2;
        if (!sj.state.compare_exchange_strong(expected, 3, std::memory_order_acq_rel)) continue;
        sj.fn(sj.context, sj.index);
        takenByOthers.fetch_add(1, std::memory_order_relaxed);
        sj.state.store(4, std::memory_order_release);
        return true;
    }
    return false;
}

void ParallelRunner::finish(int handle) noexcept
{
    if (handle < 0 || handle >= kSide) return;
    auto& sj = side[(size_t) handle];
    int expected = 2;
    if (sj.state.compare_exchange_strong(expected, 3, std::memory_order_acq_rel))
    {
        sj.fn(sj.context, sj.index); // nobody took it: do it here
        sj.state.store(0, std::memory_order_release);
        return;
    }
    if (sj.state.load(std::memory_order_acquire) != 4)
    {
        const auto t = std::chrono::steady_clock::now();
        while (sj.state.load(std::memory_order_acquire) != 4) cpuRelax();
        if (tlsAudioThread)
            waitedMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
    }
    sj.state.store(0, std::memory_order_release);
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
                    sleepUntilChanged(work, v);
                sleeping.fetch_sub(1, std::memory_order_acq_rel);
                v = work.load(std::memory_order_acquire);
            }
        }
        if (quit.load()) return;
        seen = (std::uint32_t) (v >> 32);
        while (claimAndRun(seen)) {}
        // no job of our own left: a job of this block may still hand us side work - watch until the block is done
        if (sideOpen.load(std::memory_order_relaxed))
            while ((std::uint32_t) (work.load(std::memory_order_acquire) >> 32) == seen && !quit.load(std::memory_order_relaxed)
                   && jobsDone.load(std::memory_order_acquire) < numJobsNow.load(std::memory_order_relaxed))
                if (!takeSide()) cpuRelax();
    }
}

} // namespace ampsurd
