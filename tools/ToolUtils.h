// Shared helpers for the command-line tools (not used by the plugin).
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "wav.h" // AudioDSPTools

namespace tools
{
inline bool loadWav(const std::string& path, std::vector<float>& audio, double& sampleRate)
{
    return dsp::wav::Load(path.c_str(), audio, sampleRate) == dsp::wav::LoadReturnCode::SUCCESS;
}

// Mono 32-bit float WAV.
inline bool saveWav(const std::string& path, const std::vector<float>& x, double sampleRate)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t dataSize = (uint32_t) (x.size() * sizeof(float));
    out.write("RIFF", 4); u32(36 + dataSize); out.write("WAVE", 4);
    out.write("fmt ", 4); u32(16); u16(3); u16(1); u32((uint32_t) sampleRate);
    u32((uint32_t) sampleRate * 4); u16(4); u16(32);
    out.write("data", 4); u32(dataSize);
    out.write(reinterpret_cast<const char*>(x.data()), dataSize);
    return (bool) out;
}
} // namespace tools
