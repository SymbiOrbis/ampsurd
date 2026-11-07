#pragma once

// AMPSURD IRs (2026-10-28): the owner's synthetic 4x12 cabinet IR set, calculated live.
//
// The set (AMPSURD_IRs v6, generator make_irs.py by the owner) is not recorded: every IR is a minimum-phase
// filter built from parametric EQ models of a speaker (BRIT 30, HEAVY 77, BAYOU), a microphone (DYN, DYN WIDE, RIBBON),
// the mic's distance from the speaker centre (25 .. 100 mm, cap edge -> cone), a common voicing and a smooth
// per-combination "cone ripple". This class is an exact port of that generator (tested against the 144 WAV
// files: same shape to ~1e-9), so AMPSURD needs no IR files and can make ANY point between them:
//
//   row  0 .. 8  = speaker x mic (BRIT 30 DYN, BRIT 30 RIBBON, BRIT 30 DYN WIDE, HEAVY 77 ..., BAYOU ...); between two rows the
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
    // the simple form (until 0.9.7): between rows floor(row) and floor(row) + 1, both at `mm`
    double row = 0.0; // 0 .. kRows - 1
    double mm = 25.0; // 25 .. 100
    // the pair form (0.9.8, sliding rows on the map): when rowB >= 0 the IR is (1 - t) x [row `row` at `mm`]
    // + t x [row `rowB` at `mmB`] (blended in dB like neighbouring rows); `row` is then a whole row number
    int rowB = -1;
    double mmB = 25.0, t = 0.0;
    // the map it was chosen on (0.9.8): display row d shows model row order[d], slid by offset[d] boxes; x / y =
    // the point in box units (box centre = whole numbers: x 0..15, y 0..8). Only for the map; the sound is the above.
    bool hasLayout = false;
    std::array<int, 9> order { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    std::array<int, 9> offset {};
    double x = 0.0, y = 0.0;
    bool operator==(const CabPoint& o) const
    {
        return row == o.row && mm == o.mm && rowB == o.rowB && mmB == o.mmB && t == o.t && hasLayout == o.hasLayout
            && (!hasLayout || (order == o.order && offset == o.offset && x == o.x && y == o.y));
    }
    bool sameSound(const CabPoint& o) const { return row == o.row && mm == o.mm && rowB == o.rowB && mmB == o.mmB && t == o.t; }
};

class CabIrModel
{
public:
    static constexpr int kSpeakers = 3, kMics = 3, kRows = kSpeakers * kMics;
    static constexpr int kIrLength = 2048, kFftSize = 16384;
    static constexpr double kMinMm = 25.0, kMaxMm = 100.0, kStepMm = 5.0;
    static const char* speakerName(int s);      // "BRIT 30", "HEAVY 77", "BAYOU" (own names, owner 2026-11-07: no trademarks)
    static const char* micName(int m);          // "DYN", "DYN WIDE", "RIBBON"
    static const char* speakerFileTag(int s);   // in exported file names: "BRIT30", "HEAVY77", "BAYOU"
    static const char* micFileTag(int m);       // "57", "421", "121"
    static int speakerOfRow(int row) { return row / kMics; }
    // mic order on the map, per speaker: DYN, RIBBON, DYN WIDE (owner 2026-10-29) - mic index = the model's
    // (0 DYN, 1 DYN WIDE, 2 RIBBON), row = speaker * 3 + position of the mic on the map
    static int micOfRow(int row) { static const int m[kMics] { 0, 2, 1 }; return m[row % kMics]; }
    static int rowOf(int speaker, int micIndex) { static const int pos[kMics] { 0, 2, 1 }; return speaker * kMics + pos[micIndex]; }

    static CabPoint clamp(CabPoint p);

    // The map (0.9.8): 16 boxes per row, rows can be reordered and slid by whole boxes. A row runs there and
    // back - 25, 30 .. 100, 95 .. 25, 30 .. - so sliding it endlessly always changes the position smoothly.
    static constexpr int kColumns = 16;
    // the mic position shown at x (box units, box centre = whole number) in a row slid by `offset` boxes;
    // between box centres it moves linearly (= the sound in between)
    static double mmOnMap(int offset, double x);
    // the IR at a point of a map (pair form + the layout); a box centre = that one IR alone
    static CabPoint atMap(const std::array<int, 9>& order, const std::array<int, 9>& offset, double x, double y);
    // a simple point (old presets) on the default map (rows in order, not slid)
    static CabPoint onDefaultMap(CabPoint simple);
    static bool isValidOrder(const std::array<int, 9>& order);
    // "BRIT 30 + DYN, 45 mm" or, between rows, "BRIT 30 + 70 % DYN / 30 % DYN WIDE, 47.5 mm"
    static std::string describe(CabPoint p);
    // a file name like the original set: "AMPSURD-412-BRIT30-DYN-P045mm" (a point between rows / positions gets
    // both tags and the exact position, e.g. "AMPSURD-412-BRIT30-DYN+BRIT30-DYNWIDE-P047.5mm")
    static std::string fileName(CabPoint p);
    // "ampsurd-ir2:<row>,<mm>" - how presets refer to it (no file needed). "ampsurd-ir:" (test builds of
    // 2026-10-28, mic order DYN / DYN WIDE / RIBBON) is still read: its row goes to the same speaker and nearest mic.
    // The pair form / a map: "ampsurd-ir3:<row>,<mm>,<rowB>,<mmB>,<t>;<x>,<y>;<order x9>;<offset x9>" (0.9.8).
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
