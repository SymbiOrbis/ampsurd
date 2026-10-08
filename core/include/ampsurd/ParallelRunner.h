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

    // Audio thread. Runs fn(context, 0..numJobs-1) and returns when all are done.
    void run(int numJobs, JobFn fn, void* context) noexcept;

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
};

} // namespace ampsurd
