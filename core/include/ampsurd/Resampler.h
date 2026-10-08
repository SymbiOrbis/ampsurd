#pragma once

// StreamResampler: high-quality sample-rate conversion of a stream (export, backing tracks).
// Polyphase Kaiser-windowed sinc (128 taps at the lower rate, 512 phases, linear interpolation
// between phases), band-limited to the lower of the two Nyquist frequencies: level exact, distortion
// and aliasing below -100 dB (fx_test). Not real-time (offline / background thread only).

#include <vector>

namespace ampsurd
{

class StreamResampler
{
public:
    StreamResampler(double fromRate, double toRate, int channels);

    // Feeds n input frames (channel-planar pointers) and appends the produced output frames to `out`
    // (one vector per channel). Call finish() after the last input to flush the filter.
    void process(const float* const* in, int n, std::vector<std::vector<float>>& out);
    void finish(std::vector<std::vector<float>>& out);

    double ratio() const { return outRate / inRate; }
    bool isIdentity() const { return identity; }

private:
    void produce(std::vector<std::vector<float>>& out, bool flushing);
    double inRate, outRate;
    int numChannels;
    bool identity;
    static constexpr int kPhases = 512;
    std::vector<float> table; // (kPhases + 1) x taps
    double step = 1.0;        // input samples per output sample
    long long outCount = 0;   // output samples produced so far
    long long trimmed = 0;    // input history samples dropped so far
    std::vector<std::vector<float>> hist;
    double scale = 1.0;
};

} // namespace ampsurd
