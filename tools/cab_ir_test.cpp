// cab_ir_test: the AMPSURD IR model (core/CabIrModel) against the owner's generated IR set (AMPSURD_IRs v6).
//   cab_ir_test [dir]   dir = a folder with the set's "48k" / "44.1k" sub-folders (or the WAVs directly);
//                       default: tests/data/ampsurd_irs (a few reference files from the set, in CI)
// Checks: every file = the model at its row / position (shape, after unit-energy normalisation, as AMPSURD
// uses IRs); points between grid points lie between their neighbours; any sample rate; speed.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#include "ToolUtils.h"
#include "ampsurd/CabIrModel.h"

using ampsurd::CabIrModel;
using ampsurd::CabPoint;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const std::string& what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

static std::vector<double> unitEnergy(std::vector<double> x)
{
    double e = 0.0;
    for (double v : x) e += v * v;
    const double g = e > 0.0 ? 1.0 / std::sqrt(e) : 0.0;
    for (auto& v : x) v *= g;
    return x;
}

// magnitude (dB) at frequency f of an IR
static double magDb(const std::vector<double>& ir, double f, double sr)
{
    double re = 0.0, im = 0.0;
    for (size_t n = 0; n < ir.size(); ++n)
    {
        re += ir[n] * std::cos(2.0 * M_PI * f * (double) n / sr);
        im -= ir[n] * std::sin(2.0 * M_PI * f * (double) n / sr);
    }
    return 10.0 * std::log10(re * re + im * im + 1e-30);
}

int main(int argc, char** argv)
{
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::path(AMPSURD_SOURCE_DIR) / "tests" / "data" / "ampsurd_irs";
    const std::regex nameRe("AMPSURD-412-(30|77|SWAMP)-(57|421|121)-P([0-9]+)mm\\.wav");
    int files = 0;
    double worst = 0.0;
    std::string worstName;
    if (fs::exists(dir))
        for (const auto& entry : fs::recursive_directory_iterator(dir))
        {
            std::smatch m;
            const auto name = entry.path().filename().string();
            if (!entry.is_regular_file() || !std::regex_match(name, m, nameRe)) continue;
            const int spk = m[1] == "30" ? 0 : m[1] == "77" ? 1 : 2;
            const int mic = m[2] == "57" ? 0 : m[2] == "421" ? 1 : 2;
            const double mm = std::stod(m[3]);
            std::vector<float> audio;
            double sr = 0.0;
            if (!tools::loadWav(entry.path().string(), audio, sr)) { check(false, "read " + name); continue; }
            const auto ref = unitEnergy(std::vector<double>(audio.begin(), audio.end()));
            const auto mine = unitEnergy(CabIrModel::make({ (double) (spk * CabIrModel::kMics + mic), mm }, sr));
            double d = ref.size() == mine.size() ? 0.0 : 1.0;
            for (size_t i = 0; i < std::min(ref.size(), mine.size()); ++i) d = std::max(d, std::abs(ref[i] - mine[i]));
            if (d > worst) { worst = d; worstName = name + " @ " + std::to_string((int) sr); }
            if (CabIrModel::fileName({ (double) (spk * CabIrModel::kMics + mic), mm }) + ".wav" != name)
                check(false, "file name for " + name + ": " + CabIrModel::fileName({ (double) (spk * CabIrModel::kMics + mic), mm }));
            ++files;
        }
    std::printf("compared %d files from %s, largest sample difference %.2e (%s)\n", files, dir.string().c_str(), worst, worstName.c_str());
    // 24-bit files: quantisation alone is ~1e-7 of full scale
    check(files > 0 && worst < 2e-5, "the model = the owner's IR files (" + std::to_string(files) + " files)");

    // between grid points: 47.5 mm lies between 45 and 50 mm; row 0.5 between V30 SM57 and V30 MD421 (at 5 kHz)
    {
        const double sr = 48000.0;
        auto at = [&](CabPoint p, double f) { return magDb(unitEnergy(CabIrModel::make(p, sr)), f, sr); };
        bool between = true;
        for (double f : { 120.0, 800.0, 3000.0, 5000.0, 8000.0 })
        {
            const double a = at({ 0, 45 }, f), b = at({ 0, 50 }, f), c = at({ 0, 47.5 }, f);
            const double x = at({ 0, 60 }, f), y = at({ 1, 60 }, f), z = at({ 0.5, 60 }, f);
            between = between && c >= std::min(a, b) - 0.05 && c <= std::max(a, b) + 0.05
                      && z >= std::min(x, y) - 0.05 && z <= std::max(x, y) + 0.05;
        }
        check(between, "points between the grid lie between their neighbours (position and mic blend)");
    }
    // any rate; finite; names / refs
    {
        bool ok = true;
        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            const auto ir = CabIrModel::make({ 4.3, 66.0 }, sr);
            double e = 0.0;
            for (double v : ir) ok = ok && std::isfinite(v), e += v * v;
            ok = ok && e > 1e-12 && ir.size() == (size_t) CabIrModel::kIrLength;
        }
        check(ok, "IRs at 44.1 / 48 / 88.2 / 96 / 192 kHz are finite and not silent");
        CabPoint p;
        const bool refOk = CabIrModel::fromRef(CabIrModel::toRef({ 3.25, 47.5 }), p) && std::abs(p.row - 3.25) < 1e-6
                           && std::abs(p.mm - 47.5) < 1e-6 && !CabIrModel::fromRef("C:/irs/x.wav", p);
        std::printf("  %s | %s | %s\n", CabIrModel::describe({ 0, 25 }).c_str(), CabIrModel::describe({ 0.3, 47.5 }).c_str(),
                    CabIrModel::describe({ 2.5, 100 }).c_str());
        check(refOk && CabIrModel::describe({ 0, 25 }) == "V30 + SM57, 25 mm"
                  && CabIrModel::describe({ 0.3, 47.5 }) == "V30 + 70 % SM57 / 30 % MD421, 47.5 mm",
              "preset reference and names");
    }
    // speed (a new point while dragging must be quick)
    {
        CabIrModel::make({ 0, 25 }, 48000.0); // cache
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) CabIrModel::make({ i * 0.4, 25.0 + i * 3.7 }, 48000.0);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20.0;
        std::printf("one IR: %.1f ms\n", ms);
        check(ms < 60.0, "a new IR in well under 0.1 s");
    }
    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
