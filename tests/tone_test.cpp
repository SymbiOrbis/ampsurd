// tone_test: does AMPSURD with ONE capture (everything else neutral) sound exactly like the
// capture run directly through NeuralAmpModelerCore (what the official NAM plugin does)?
// Feeds the same guitar-like signal to both, aligns by AMPSURD's reported latency (and searches
// +-64 samples), then compares level, residual and the spectrum in octave bands.
//
// Usage: tone_test <capture.nam> [rate] [block] [interfaceDbu: also test input calibration]
// Exit 1 at 48 kHz if AMPSURD differs from NAM Core by more than -90 dB.

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iostream>
#include <random>

#include "../plugin/PluginProcessor.h"
#include "ampsurd/CaptureModel.h"
#include "ampsurd/Convolver.h"

static void setP(AmpsurdProcessor& p, const juce::String& id, float v)
{
    if (auto* rp = p.params.getParameter(id)) rp->setValueNotifyingHost(rp->convertTo0to1(v));
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::cerr << "Usage: tone_test <capture.nam> [rate] [block]\n"; return 1; }
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = argc > 2 ? std::atof(argv[2]) : 48000.0;
    const int block = argc > 3 ? std::atoi(argv[3]) : 128;
    const int N = (int) (sr * 6.0);

    // input: plucked notes with rich harmonics + palm-mute-like short low notes, peaks ~ -6 dBFS
    std::vector<double> x((size_t) N);
    std::mt19937 rng(1);
    std::normal_distribution<double> nd(0.0, 1.0);
    double ph = 0;
    for (int i = 0; i < N; ++i)
    {
        const double t = std::fmod(i / sr, 0.4);
        const int k = (i / (int) (0.4 * sr)) % 6;
        const double f = 82.41 * std::pow(2.0, (k * 5) / 12.0);
        ph += f / sr;
        double s = 0;
        for (int h = 1; h <= 12; ++h) s += std::sin(2 * juce::MathConstants<double>::pi * h * ph) / (h * h * 0.5 + 0.5);
        x[(size_t) i] = 0.35 * s * std::exp(-(k % 2 ? 12.0 : 3.0) * t) + 0.002 * nd(rng);
    }

    // A: raw NAM core (with calibration: the input scaled by interface level - capture level)
    auto r = ampsurd::CaptureModel::load(argv[1], sr, block);
    if (!r.model) { std::cerr << r.error << "\n"; return 2; }
    const bool calibrate = argc > 4;
    const double interfaceDbu = calibrate ? std::atof(argv[4]) : 12.0;
    double trimDb = 0.0;
    if (calibrate && r.model->getInfo().hasInputLevel) trimDb = interfaceDbu - r.model->getInfo().inputLevelDbu;
    std::vector<double> xa(x);
    for (double& v : xa) v *= std::pow(10.0, trimDb / 20.0);
    std::vector<double> a((size_t) N);
    for (int s = 0; s < N; s += block)
    {
        const int n = std::min(block, N - s);
        r.model->process(xa.data() + s, a.data() + s, n, false);
    }
    const int rawLat = r.model->getLatencySamples();

    // B: AMPSURD, one capture, gate off, level match off, everything else default
    auto proc = std::make_unique<AmpsurdProcessor>();
    proc->setPlayConfigDetails(1, 2, sr, block);
    proc->prepareToPlay(sr, block);
    setP(*proc, "gateOn", 0.0f);
    setP(*proc, "levelMatch", 0.0f);
    const bool wasOn = proc->isInputCalibrationOn();
    const double wasDbu = proc->getInterfaceInputDbu();
    proc->setInputCalibration(calibrate, interfaceDbu);
    if (calibrate) std::printf("input calibration: interface %+.1f dBu -> capture driven %+.2f dB\n", interfaceDbu, trimDb);
    proc->loadCapture(0, juce::File(argv[1]));
    for (int t = 0; t < 400 && proc->getSlotStatus(0).state == AmpsurdProcessor::SlotState::loading; ++t)
        juce::Thread::sleep(50);
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    for (int k = 0; k < 200; ++k) { buf.clear(); proc->processBlock(buf, midi); } // adopt + settle
    for (int k = 0; k < 20; ++k) { juce::Thread::sleep(50); buf.clear(); proc->processBlock(buf, midi); }
    const int lat = proc->getLatencySamples();
    std::vector<double> b((size_t) N), bR((size_t) N);
    for (int s = 0; s < N; s += block)
    {
        const int n = std::min(block, N - s);
        buf.clear();
        for (int i = 0; i < n; ++i) buf.setSample(0, i, (float) x[(size_t) (s + i)]);
        proc->processBlock(buf, midi);
        for (int i = 0; i < n; ++i) { b[(size_t) (s + i)] = buf.getSample(0, i); bR[(size_t) (s + i)] = buf.getSample(1, i); }
    }

    // best alignment around the reported latency difference
    const int start = (int) sr, end = N - 2000;
    int bestD = 0;
    double bestC = -1;
    for (int d = lat - rawLat - 64; d <= lat - rawLat + 64; ++d)
    {
        double c = 0, ea = 0, eb = 0;
        for (int i = start; i < end; ++i) { c += a[(size_t) i] * b[(size_t) (i + d)]; ea += a[(size_t) i] * a[(size_t) i]; eb += b[(size_t) (i + d)] * b[(size_t) (i + d)]; }
        c /= std::sqrt(ea * eb + 1e-30);
        if (c > bestC) { bestC = c; bestD = d; }
    }
    double ea = 0, eb = 0, er = 0, lr = 0;
    for (int i = start; i < end; ++i)
    {
        const double ai = a[(size_t) i], bi = b[(size_t) (i + bestD)];
        ea += ai * ai; eb += bi * bi; er += (ai - bi) * (ai - bi);
        lr += (b[(size_t) (i + bestD)] - bR[(size_t) (i + bestD)]) * (b[(size_t) (i + bestD)] - bR[(size_t) (i + bestD)]);
    }
    std::printf("rate %.0f block %d: reported latency %d (raw model %d), best offset %d\n", sr, block, lat, rawLat, bestD);
    std::printf("correlation %.6f, level AMPSURD vs NAM %+.2f dB, residual %.1f dB below the signal, L-R %.1f dB\n",
                bestC, 10 * std::log10(eb / ea), 10 * std::log10(er / ea + 1e-30), 10 * std::log10(lr / eb + 1e-30));

    // octave-band levels (simple FFT-free: one-pole band split via Goertzel-ish energy is overkill; use DFT bins)
    const int L = 1 << 15;
    const ampsurd::Fft fft(L);
    std::vector<double> ra((size_t) L), ia((size_t) L), rb((size_t) L), ib((size_t) L);
    std::vector<double> pa(L / 2), pb(L / 2);
    for (int seg = start; seg + L < end; seg += L / 2)
    {
        for (int i = 0; i < L; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / L);
            ra[(size_t) i] = w * a[(size_t) (seg + i)]; ia[(size_t) i] = 0;
            rb[(size_t) i] = w * b[(size_t) (seg + i + bestD)]; ib[(size_t) i] = 0;
        }
        fft.forward(ra.data(), ia.data());
        fft.forward(rb.data(), ib.data());
        for (int k = 0; k < L / 2; ++k)
        {
            pa[(size_t) k] += ra[(size_t) k] * ra[(size_t) k] + ia[(size_t) k] * ia[(size_t) k];
            pb[(size_t) k] += rb[(size_t) k] * rb[(size_t) k] + ib[(size_t) k] * ib[(size_t) k];
        }
    }
    std::printf("band       NAM dB   AMPSURD-NAM dB\n");
    for (double lo = 31.25; lo < sr / 2; lo *= 2)
    {
        double sa = 0, sb = 0;
        for (int k = 1; k < L / 2; ++k)
        {
            const double f = k * sr / L;
            if (f >= lo && f < lo * 2) { sa += pa[(size_t) k]; sb += pb[(size_t) k]; }
        }
        std::printf("%6.0f Hz  %7.1f  %+7.2f\n", lo * std::sqrt(2.0), 10 * std::log10(sa + 1e-30), 10 * std::log10((sb + 1e-30) / (sa + 1e-30)));
    }
    proc->setInputCalibration(wasOn, wasDbu); // leave this computer's setting as it was
    const double residualDb = 10 * std::log10(er / ea + 1e-30);
    if (sr == 48000.0 && residualDb > -90.0) { std::printf("FAIL: AMPSURD differs from NAM Core\n"); return 1; }
    std::printf("PASS\n");
    return 0;
}
