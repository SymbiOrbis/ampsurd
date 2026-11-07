#pragma once

// ParallelRunner: runs a handful of independent jobs (the five amp paths) of one audio block on
// several CPU cores at the same time.
//
// The audio thread calls run(numJobs, fn). Worker threads and the audio thread itself claim jobs
// from a shared counter until none are left, so:
//   - if the workers are asleep or busy, the audio thread simply does every job itself (never
//     slower than single-threaded, apart from a few atomic operations);
//   - the audio thread only waits for jobs a worker has already started.
// Each claim is tagged with the block's generation, so a late worker can never take a job of the
// wrong block. Workers spin briefly after each block (the next block follows within milliseconds),
// then sleep on an atomic wait. They run at real-time priority ("Pro Audio" on Windows) with
// denormals flushed to zero, like the audio thread.
//
// start()/stop(): non-RT (prepare / destructor). run(): audio thread only, no allocation, no locks.

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace ampsurd
{

class ParallelRunner
{
public:
    using JobFn = void (*)(void* context, int job) noexcept;

    ParallelRunner() = default;
    ~ParallelRunner();
    ParallelRunner(const ParallelRunner&) = delete;
    ParallelRunner& operator=(const ParallelRunner&) = delete;

    // Starts `workers` threads (0 = run everything on the calling thread). Non-RT.
    void start(int workers);
    void stop();
    int numWorkers() const noexcept { return (int) threads.size(); }
    // audio thread: all the time run() so far waited for jobs a helper core had started (ms, keeps growing).
    // For the log: a block that waited long = a helper thread was paused by the computer.
    double waitedMsTotal() const noexcept { return waitedMs; }

    // Audio thread. Runs fn(context, 0..numJobs-1) and returns when all are done.
    // sideWork: a job may hand part of its work to a free core (offer / finish below); idle helpers then keep
    // watching for it until the block is done (otherwise they do not spin for nothing).
    void run(int numJobs, JobFn fn, void* context, bool sideWork = false) noexcept;

    // Side work (owner 2026-11-07: an AMPSURD amp between two GAIN positions runs its two captures on two cores).
    // Called from inside a job: offer() hands fn(context, index) to any free core; finish() runs it right here if
    // no core took it yet, otherwise waits until that core is done. A job that is offered is ALWAYS done once
    // finish() returns - with no free core it simply runs in finish(), as if it had never been offered.
    // offer() returns -1 when there is no helper at all (run it yourself). No allocation, no locks.
    int offer(JobFn fn, void* context, int index) noexcept;
    void finish(int handle) noexcept;
    // the runner whose job is running on this thread (nullptr outside a job)
    static ParallelRunner* current() noexcept;
    // tests: side jobs a helper core (not their owner) did
    int sideTakenByOthers() const noexcept { return takenByOthers.load(std::memory_order_relaxed); }

    // Worker threads worth starting on this computer: cores - 1, at most `maxUseful`; 0 below 4 cores.
    static int recommendedWorkers(int maxUseful) noexcept;

private:
    void workerLoop();
    bool claimAndRun(std::uint32_t gen) noexcept; // false when no job of `gen` is left

    std::vector<std::thread> threads;
    std::atomic<bool> quit { false };
    // high 32 bits: block generation, low 32 bits: next job index
    std::atomic<std::uint64_t> work { 0 };
    std::atomic<int> jobsDone { 0 };
    // written before `work` is published (release); read after a successful claim (acquire)
    std::atomic<int> numJobsNow { 0 };
    std::atomic<JobFn> fnNow { nullptr };
    std::atomic<void*> ctxNow { nullptr };
    std::atomic<int> sleeping { 0 };
    double waitedMs = 0.0; // audio thread only

    // side jobs: 0 free, 1 being filled, 2 offered, 3 running, 4 done (by a helper)
    struct Side
    {
        std::atomic<int> state { 0 };
        JobFn fn = nullptr;
        void* context = nullptr;
        int index = 0;
    };
    static constexpr int kSide = 16;
    std::array<Side, kSide> side;
    std::atomic<bool> sideOpen { false };
    std::atomic<int> takenByOthers { 0 };
    bool takeSide() noexcept; // a helper (or a waiting thread) does one offered side job; false = none
};

} // namespace ampsurd
