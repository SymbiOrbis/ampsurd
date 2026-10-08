// soak_test: runs the real AMPSURD processor for a long time (faster than real time) and reports,
// per simulated minute, how long the audio callback takes (mean / 99.9th percentile / max).
// A cost that grows over time shows up as rising numbers. A second thread does what the editor
// does 30 times a second (spectrum, tuner, meters).
//
// Usage: soak_test <capture.nam> [minutes] [block]

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include "../plugin/PluginProcessor.h"

static void setP(AmpsurdProcessor& p, const juce::String& id, float v)
{
    if (auto* rp = p.params.getParameter(id)) rp->setValueNotifyingHost(rp->convertTo0to1(v));
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::cerr << "Usage: soak_test <capture.nam> [minutes] [block]\n"; return 1; }
    juce::ScopedJuceInitialiser_GUI init;
    const double minutes = argc > 2 ? std::atof(argv[2]) : 20.0;
    const int block = argc > 3 ? std::atoi(argv[3]) : 128;
    const double sr = 48000.0;

    auto proc = std::make_unique<AmpsurdProcessor>();
    proc->prepareToPlay(sr, block);
    proc->loadCapture(0, juce::File(argv[1]));
    for (int t = 0; t < 400 && proc->getSlotStatus(0).state == AmpsurdProcessor::SlotState::loading; ++t)
        juce::Thread::sleep(50);
    if (const char* ir = std::getenv("SOAK_IR"))
    {
        proc->loadIr(0, juce::File(ir));
        juce::Thread::sleep(1500);
    }
    if (const char* fx = std::getenv("SOAK_FX"); fx != nullptr && fx[0] == '1')
    {
        setP(*proc, "fxD1On", 1.0f); setP(*proc, "fxD2On", 1.0f); setP(*proc, "fxRevOn", 1.0f); setP(*proc, "fxFlOn", 1.0f);
    }

    std::atomic<bool> stop { false };
    std::thread ui([&] {
        std::vector<float> mags;
        double binHz = 0;
        const bool noUi = std::getenv("SOAK_NOUI") != nullptr;
        while (!stop.load() && !noUi)
        {
            proc->getInputSpectrum(mags, binHz);
            proc->analyseTuner();
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    });

    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    const long long perMinute = (long long) (60.0 * sr / block);
    const double budgetUs = 1e6 * block / sr;
    std::vector<double> times;
    times.reserve((size_t) perMinute);
    long long sample = 0;
    double phase = 0.0;
    float peakOut = 0.0f;
    bool finite = true;
    std::printf("block %d @ 48 kHz, budget %.0f us\n", block, budgetUs);
    std::printf("%-6s %-10s %-10s %-10s %-8s\n", "min", "mean us", "p99.9 us", "max us", "peak");
    for (int m = 0; m < (int) std::ceil(minutes); ++m)
    {
        times.clear();
        peakOut = 0.0f;
        for (long long b = 0; b < perMinute; ++b)
        {
            for (int i = 0; i < block; ++i, ++sample)
            {
                // a note every 0.5 s on a changing pitch, decaying, plus a little noise between notes
                const double t = std::fmod((double) sample / sr, 0.5);
                const int note = (int) ((sample / (long long) (sr / 2)) % 12);
                const double f = 82.41 * std::pow(2.0, note / 12.0 * 2.0);
                phase += f / sr;
                if (phase > 1.0) phase -= 1.0;
                const float v = (float) (0.3 * std::sin(2 * juce::MathConstants<double>::pi * phase) * std::exp(-3.0 * t)
                                         + 1e-4 * (std::rand() / (double) RAND_MAX - 0.5));
                buf.setSample(0, i, v);
                buf.setSample(1, i, v);
            }
            const auto t0 = std::chrono::steady_clock::now();
            proc->processBlock(buf, midi);
            const auto t1 = std::chrono::steady_clock::now();
            times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i)
                {
                    const float y = buf.getSample(c, i);
                    finite = finite && std::isfinite(y);
                    peakOut = std::max(peakOut, std::abs(y));
                }
        }
        if (m == 0 && std::getenv("SOAK_DUMP") != nullptr)
        {
            std::vector<std::pair<double, long long>> idx;
            for (long long k = 0; k < (long long) times.size(); ++k) idx.push_back({ times[(size_t) k], k });
            std::sort(idx.rbegin(), idx.rend());
            for (int k = 0; k < 40; ++k) std::printf("  spike %.0f us at block %lld\n", idx[(size_t) k].first, idx[(size_t) k].second);
        }
        double mean = 0;
        for (double x : times) mean += x;
        mean /= (double) times.size();
        std::sort(times.begin(), times.end());
        std::printf("%-6d %-10.1f %-10.1f %-10.1f %-8.3f\n", m + 1, mean,
                    times[(size_t) (0.999 * (double) (times.size() - 1))], times.back(), peakOut);
        std::fflush(stdout);
    }
    stop = true;
    ui.join();
    std::printf("output finite: %s\n", finite ? "yes" : "NO");
    return finite ? 0 : 1;
}
