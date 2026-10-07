// ampsurd_bench: CPU cost of 1..5 parallel captures at different sample rates and
// buffer sizes, plus a real-time-safety stress test of capture hot-swapping.
//
// Usage:
//   ampsurd_bench <model.nam> [--slots N] [--seconds S]
//   ampsurd_bench <model.nam> --stress [--seconds S] [<other.nam> ...]

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "ampsurd/CaptureModel.h"
#include "ampsurd/CaptureSlot.h"

using namespace ampsurd;

static std::vector<double> makeTestSignal(double sr, double seconds)
{
    // Plucked-string-like test signal: decaying harmonics, repeated every 0.5 s.
    std::vector<double> x((size_t) (sr * seconds));
    const double f0 = 110.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = std::fmod((double) i / sr, 0.5);
        double s = 0;
        for (int h = 1; h <= 8; ++h)
            s += std::sin(2 * 3.14159265358979323846 * f0 * h * t) / h;
        x[i] = 0.3 * s * std::exp(-4.0 * t);
    }
    return x;
}

static int bench(const std::string& path, int slots, double seconds)
{
    std::printf("%-8s %-6s %-6s %-12s %-10s\n", "rate", "block", "slots", "%realtime", "us/block");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const auto signal = makeTestSignal(sr, seconds);
        for (int block : { 32, 64, 128, 256 })
        {
            std::vector<std::unique_ptr<CaptureSlot>> bank;
            for (int s = 0; s < slots; ++s)
            {
                auto r = CaptureModel::load(path, sr, block);
                if (!r.model) { std::cerr << "LOAD FAILED: " << r.error << "\n"; return 2; }
                bank.push_back(std::make_unique<CaptureSlot>());
                bank.back()->submit(std::move(r.model));
                bank.back()->prepare(sr, block);
            }
            std::vector<double> out((size_t) block), mix((size_t) block);
            size_t blocks = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t pos = 0; pos + (size_t) block <= signal.size(); pos += (size_t) block, ++blocks)
            {
                std::fill(mix.begin(), mix.end(), 0.0);
                for (auto& slot : bank)
                {
                    slot->process(signal.data() + pos, out.data(), block, true);
                    for (int i = 0; i < block; ++i) mix[(size_t) i] += out[(size_t) i] / slots;
                }
            }
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            const double audio = (double) (blocks * (size_t) block) / sr;
            std::printf("%-8.0f %-6d %-6d %-12.1f %-10.1f\n", sr, block, slots, 100.0 * elapsed / audio,
                        1e6 * elapsed / (double) blocks);
            std::fflush(stdout);
        }
    }
    return 0;
}

static int stress(const std::vector<std::string>& paths, double seconds)
{
    const double sr = 44100.0; // forces the resampling path
    const int maxBlock = 256;
    CaptureSlot slot;
    {
        auto r = CaptureModel::load(paths[0], sr, maxBlock);
        if (!r.model) { std::cerr << r.error << "\n"; return 2; }
        slot.submit(std::move(r.model));
    }
    slot.prepare(sr, maxBlock);

    std::atomic<bool> running { true };
    std::atomic<int> swaps { 0 };
    std::thread loader([&] {
        size_t k = 0;
        while (running)
        {
            const auto& p = paths[++k % paths.size()];
            auto m = (k % 7 == 0) ? CaptureModel::makeEmpty(sr, maxBlock) : CaptureModel::load(p, sr, maxBlock).model;
            if (m) { slot.submit(std::move(m)); ++swaps; }
            slot.collectGarbage();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    });

    const auto signal = makeTestSignal(sr, 1.0);
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> blockDist(1, maxBlock);
    std::vector<double> out((size_t) maxBlock);
    size_t pos = 0, processed = 0, bad = 0;
    double peak = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds)
    {
        const int n = blockDist(rng);
        if (pos + (size_t) n > signal.size()) pos = 0;
        slot.process(signal.data() + pos, out.data(), n, true);
        for (int i = 0; i < n; ++i)
        {
            if (!std::isfinite(out[(size_t) i])) ++bad;
            peak = std::max(peak, std::abs(out[(size_t) i]));
        }
        pos += (size_t) n;
        processed += (size_t) n;
    }
    running = false;
    loader.join();
    slot.collectGarbage();
    std::printf("stress: %d capture swaps, %.1f s of audio, random block sizes 1..%d, non-finite samples: %zu, peak %.3f\n",
                swaps.load(), (double) processed / sr, maxBlock, bad, peak);
    return bad == 0 ? 0 : 3;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: ampsurd_bench <model.nam> [--slots N] [--seconds S] | --stress [more.nam ...]\n";
        return 1;
    }
    int slots = 1;
    double seconds = 4.0;
    bool doStress = false;
    std::vector<std::string> paths { argv[1] };
    for (int i = 2; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--slots") && i + 1 < argc) slots = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--stress")) doStress = true;
        else paths.emplace_back(argv[i]);
    }
    return doStress ? stress(paths, seconds) : bench(paths[0], slots, seconds);
}
