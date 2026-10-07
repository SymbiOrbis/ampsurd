#pragma once

// PitchDetector: chromatic tuner on the clean DI signal.
//
// The audio thread only copies samples into a lock-free ring buffer (push). The analysis
// (YIN pitch estimation, de Cheveigné & Kawahara 2002) runs on a non-audio thread, e.g. the
// UI timer, so the tuner costs the audio thread almost nothing. Range ~25 Hz - 1.5 kHz:
// covers 8-string guitars (F#1 = 46 Hz) and bass (B0 = 31 Hz) up to the 24th fret.

#include <atomic>
#include <string>
#include <vector>

namespace ampsurd
{

class PitchDetector
{
public:
    struct Result
    {
        bool valid = false;
        double frequencyHz = 0.0;
        int midiNote = 0;      // 69 = A4
        double cents = 0.0;    // -50..+50 from the nearest note
        double levelDb = -120.0;
        std::string noteName() const;   // e.g. "E"
        int octave() const;             // e.g. 2 for E2
    };

    void prepare(double sampleRate);           // non-RT
    void push(const float* x, int n) noexcept;  // audio thread
    Result analyse(double referenceA4 = 440.0); // non-RT

private:
    std::vector<float> ring;
    std::atomic<int> writePos { 0 };
    double sr = 48000.0;
    std::vector<double> frame, diff;
};

} // namespace ampsurd
