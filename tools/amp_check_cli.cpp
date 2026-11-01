// Developer tool: loads an .ampsurdamp like AMPSURD does (read, load each capture, GAIN morph, tone filter) and
// prints per-step loudness, the loudness across GAIN and the tone filter's effect. ampsurd_amp_check <file.ampsurdamp>
#include <cmath>
#include <iostream>
#include <juce_core/juce_core.h>
#include "AmpProfileFile.h"
#include "ampsurd/CaptureAnalyzer.h"
#include "ampsurd/CaptureModel.h"
#include "ampsurd/Engine.h"
#include <algorithm>

using namespace ampsurd;
int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    amp::Profile prof;
    juce::String err;
    if (!amp::read(juce::File(juce::String::fromUTF8(argv[1])), prof, err)) { std::cerr << "read failed: " << err << "\n"; return 1; }
    const double sr = 48000.0;
    const int block = 512;
    auto test = CaptureAnalyzer::makeTestSignal(sr);
    std::vector<CaptureModel::MorphStep> steps;
    for (auto& st : prof.steps)
    {
        auto r = CaptureModel::load(st.nam.getFullPathName().toStdString(), sr, block, false);
        if (!r.model) { std::cerr << "load failed: " << st.nam.getFileName() << " " << r.error << "\n"; return 1; }
        const auto out = CaptureAnalyzer::render(*r.model, test, false);
        bool finite = true;
        for (double v : out) finite = finite && std::isfinite(v);
        std::cout << "step knob " << st.knob << "  " << st.nam.getFileName() << "  loudness "
                  << juce::String(CaptureAnalyzer::loudnessDb(*CaptureAnalyzer::measure(out, sr)), 1) << " dB" << (finite ? "" : "  NOT FINITE") << "\n";
        r.model->prepare(sr, block);
        steps.push_back({ std::move(r.model), st.knob, 1.0, 1.0, 0.0 });
    }
    // calibrated exactly as AMPSURD does when it loads the amp (unless --plain: the blend without compensation)
    CaptureAnalyzer::calibrateMorph(steps, sr, block, 1.0);
    const bool plain = argc > 2 && juce::String(argv[2]) == "--plain";
    for (auto& st : steps)
    {
        if (plain) { st.delay = std::round(st.delay); st.pairLoss = 0.0; }
        if (argc > 2 && juce::String(argv[2]) == "--nocomp") st.pairLoss = 0.0;
        std::cout << "step knob " << st.knob << ": gain " << juce::String(st.gain, 3) << ", polarity " << st.polarity << ", delay "
                  << juce::String(st.delay, 2) << " samples, blend loss to the next " << juce::String(st.pairLoss, 3) << "\n";
    }
    CaptureInfo info;
    info.displayName = prof.name.toStdString();
    auto morph = CaptureModel::makeMorph(std::move(steps), info, sr, block);
    for (double g : { 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5, 8.0 })
    {
        morph->setMorphPosition(g);
        CaptureAnalyzer::render(*morph, std::vector<double>((size_t) sr, 0.0), false); // settle the knob
        const auto out = CaptureAnalyzer::render(*morph, test, false);
        bool finite = true;
        for (double v : out) finite = finite && std::isfinite(v);
        std::cout << "GAIN " << g << ": loudness " << juce::String(CaptureAnalyzer::loudnessDb(*CaptureAnalyzer::measure(out, sr)), 1)
                  << " dB" << (finite ? "" : "  NOT FINITE") << "\n";
    }
    if (prof.tone)
    {
        for (auto k : { std::array<double, 3> { 5, 5, 5 }, std::array<double, 3> { 0, 5, 5 }, std::array<double, 3> { 10, 5, 5 },
                        std::array<double, 3> { 5, 0, 5 }, std::array<double, 3> { 5, 10, 5 }, std::array<double, 3> { 5, 5, 0 },
                        std::array<double, 3> { 5, 5, 10 }, std::array<double, 3> { 3.5, 6.5, 7 } })
        {
            const auto c = prof.tone->curveDb(k[0], k[1], k[2]);
            const auto fir = ToneStack::minPhaseFir(c, sr);
            bool finite = true;
            double mx = -1e9, mn = 1e9;
            for (double v : fir) finite = finite && std::isfinite(v);
            for (double v : c) { mx = std::max(mx, v); mn = std::min(mn, v); }
            std::cout << "tone B" << k[0] << " M" << k[1] << " T" << k[2] << ": curve " << juce::String(mn, 1) << " .. " << juce::String(mx, 1)
                      << " dB, filter " << (finite ? "ok" : "NOT FINITE") << "\n";
        }
    }
    return 0;
}
