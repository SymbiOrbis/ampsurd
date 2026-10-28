#pragma once

// AMPSURD IRs (2026-10-28): the owner's synthetic 4x12 cabinet IR set, calculated live.
//
// The set (AMPSURD_IRs v6, generator make_irs.py by the owner) is not recorded: every IR is a minimum-phase
// filter built from parametric EQ models of a speaker (V30, DV77, SWAMP), a microphone (SM57, MD421, R121),
// the mic's distance from the speaker centre (25 .. 100 mm, cap edge -> cone), a common voicing and a smooth
// per-combination "cone ripple". This class is an exact port of that generator (tested against the 144 WAV
// files: same shape to ~1e-9), so AMPSURD needs no IR files and can make ANY point between them:
//
//   row  0 .. 8  = speaker x mic (V30 SM57, V30 MD421, V30 R121, DV77 ..., SWAMP ...); between two rows the
//                  two combinations are blended (in dB, i.e. their responses are averaged on a log scale)
//   mm  25 .. 100 = mic position, continuous (the files are every 5 mm)
//
// make(): any thread, ~3-5 ms (three 16384-point FFTs); the per-row curves are cached per sample rate.
// The result is the raw IR (2048 samples, like the files, before their common level normalisation).

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ampsurd
{

struct CabPoint
{
    double row = 0.0; // 0 .. kRows - 1
    double mm = 25.0; // 25 .. 100
    bool operator==(const CabPoint& o) const { return row == o.row && mm == o.mm; }
};

class CabIrModel
{
public:
    static constexpr int kSpeakers = 3, kMics = 3, kRows = kSpeakers * kMics;
    static constexpr int kIrLength = 2048, kFftSize = 16384;
    static constexpr double kMinMm = 25.0, kMaxMm = 100.0, kStepMm = 5.0;
    static const char* speakerName(int s);      // "V30", "DV77", "SWAMP"
    static const char* micName(int m);          // "SM57", "MD421", "R121"
    static const char* speakerFileTag(int s);   // as in the file names: "30", "77", "SWAMP"
    static const char* micFileTag(int m);       // "57", "421", "121"
    static int speakerOfRow(int row) { return row / kMics; }
    static int micOfRow(int row) { return row % kMics; }

    static CabPoint clamp(CabPoint p);
    // "V30 + SM57, 45 mm" or, between rows, "V30 + 70 % SM57 / 30 % MD421, 47.5 mm"
    static std::string describe(CabPoint p);
    // a file name like the original set: "AMPSURD-412-30-57-P045mm" (a point between rows / positions gets
    // both tags and the exact position, e.g. "AMPSURD-412-30-57+30-421-P047.5mm")
    static std::string fileName(CabPoint p);
    // "ampsurd-ir:<row>,<mm>" - how presets refer to it (no file needed)
    static std::string toRef(CabPoint p);
    static bool fromRef(const std::string& ref, CabPoint& p);

    // The IR at `fs` (any rate: it is designed there, no resampling).
    static std::vector<double> make(CabPoint p, double fs);

private:
    struct RateCache
    {
        double fs = 0.0;
        std::vector<double> freqs;                       // rfft bin frequencies (bin 0 = 1 Hz, as numpy's freqz grid)
        std::array<std::vector<double>, kRows> rowDb;    // speaker + mic + POSITION + VOICING + ripple
        std::vector<double> refPositionDb;               // position stages at 25 mm
    };
    static std::shared_ptr<const RateCache> cacheFor(double fs);
    static std::mutex cacheMutex;
    static std::shared_ptr<const RateCache> lastCache;
};

} // namespace ampsurd
