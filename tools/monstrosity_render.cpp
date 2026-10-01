// monstrosity_render: run a .nam capture over a WAV file through MONSTROSITY's own
// engine (CaptureModel + CaptureSlot), exactly as the plugin does on the audio thread.
//
// Used to prove correctness against NeuralAmpModelerCore's reference `render` tool.
//
// Usage: monstrosity_render <model.nam> <input.wav> <output.wav> [--block N] [--normalise]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "ToolUtils.h"
#include "monstrosity/CaptureModel.h"
#include "monstrosity/CaptureSlot.h"

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::cerr << "Usage: monstrosity_render <model.nam> <input.wav> <output.wav> [--block N] [--normalise]\n";
        return 1;
    }
    int block = 64;
    bool normalise = false;
    for (int i = 4; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--block") && i + 1 < argc) block = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--normalise")) normalise = true;
    }

    std::vector<float> input;
    double sr = 0;
    if (!tools::loadWav(argv[2], input, sr))
    {
        std::cerr << "Cannot read input WAV\n";
        return 1;
    }

    auto res = monstrosity::CaptureModel::load(argv[1], sr, block);
    if (!res.model)
    {
        std::cerr << "LOAD FAILED: " << res.error << "\n";
        return 2;
    }
    const auto& info = res.model->getInfo();
    std::cerr << "Loaded: " << info.displayName << " | " << info.architectureHint << " | model rate "
              << info.modelSampleRate << " | host rate " << sr << " | latency " << res.model->getLatencySamples()
              << " samples\n";

    monstrosity::CaptureSlot slot;
    slot.submit(std::move(res.model));
    slot.prepare(sr, block); // adopts the model directly (no fade-in) for a bit-exact comparison

    std::vector<double> in((size_t) block), out((size_t) block);
    std::vector<float> rendered;
    rendered.reserve(input.size());
    for (size_t pos = 0; pos < input.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t>((size_t) block, input.size() - pos);
        for (int i = 0; i < n; ++i) in[(size_t) i] = input[pos + (size_t) i];
        slot.process(in.data(), out.data(), n, normalise);
        for (int i = 0; i < n; ++i) rendered.push_back((float) out[(size_t) i]);
    }

    if (!tools::saveWav(argv[3], rendered, sr))
    {
        std::cerr << "Cannot write output WAV\n";
        return 1;
    }
    return 0;
}
