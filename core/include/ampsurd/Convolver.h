#pragma once

// Convolver: zero-latency convolution with a cabinet impulse response (IR).
//
// Non-uniformly partitioned, so a 1 s IR costs a few percent of one core and the output is never
// delayed:
//   IR samples [0, 64)        direct (time-domain) FIR - this is what makes it zero-latency
//   IR samples [64, 4096)     FFT partitions of 64 samples, computed at every 64-sample boundary
//   IR samples [4096, end)    FFT partitions of 2048 samples; their result is only needed one block
//                             later, so all of its work (forward FFT pass by pass, partition products,
//                             inverse FFT pass by pass) is spread evenly over that block (no CPU spikes)
// Typical cabinet IRs (<= 4096 samples, 85 ms at 48 kHz) only use the first two parts.
//
// Construction (and reset of a fresh object) is non-RT. process()/reset() are RT-safe:
// no allocation, no locks. An empty IR makes an "empty" convolver that passes the signal through.

#include <memory>
#include <vector>

namespace ampsurd
{

// Radix-2 complex FFT with precomputed tables (RT-safe once constructed).
class Fft
{
public:
    explicit Fft(int size);
    int size() const noexcept { return n; }
    // In place, re/im arrays of length size(). Inverse is scaled by 1/size.
    void forward(double* re, double* im) const noexcept { run(re, im, false); }
    void inverse(double* re, double* im) const noexcept { run(re, im, true); }

    // The same transform in separate passes (bit reversal, then one pass per butterfly level), so
    // that large transforms can be spread over several audio blocks.
    int numPasses() const noexcept { return passes; }
    void pass(double* re, double* im, int index, bool inverse) const noexcept;

private:
    void run(double* re, double* im, bool inverse) const noexcept;
    int passes = 1;
    int n = 0;
    std::vector<int> bitrev;
    std::vector<double> cosT, sinT;
};

class Convolver
{
public:
    static constexpr int kHead = 64;
    static constexpr int kSmallBlock = 64;
    static constexpr int kSmallEnd = 4096;   // where the large partitions start
    static constexpr int kLargeBlock = 2048;

    explicit Convolver(std::vector<double> ir);
    ~Convolver();

    bool isEmpty() const noexcept { return length == 0; }
    int getLength() const noexcept { return length; }

    void reset() noexcept;
    // out may equal in.
    void process(const double* in, double* out, int n) noexcept;

private:
    struct Stage; // uniformly partitioned FFT stage
    int length = 0;
    std::vector<double> head;   // first kHead taps, reversed for the dot product
    std::vector<double> hist;   // 2 * kHead history (written twice: contiguous window)
    int histPos = 0;
    std::unique_ptr<Stage> small, large;
};

// Prepares an IR file's samples for the convolver: removes leading silence (pure delay, which would
// only add latency), resamples to the session rate, limits the length and normalises to unit
// energy (level match measures the result anyway, so different IRs end up equally loud).
struct PreparedIr
{
    std::vector<double> samples;
    int trimmedSamples = 0;   // leading silence removed (at the file's rate)
    bool truncated = false;
    bool resampled = false;
};
PreparedIr prepareImpulseResponse(const std::vector<double>& fileSamples, double fileRate, double targetRate,
                                  double maxSeconds = 1.0);

} // namespace ampsurd
