#include "AmpProfileFile.h"

#include <cmath>
#include <map>

#include <juce_audio_formats/juce_audio_formats.h>

namespace ampsurd::amp
{
namespace
{
const char* kKnobNames[3] { "bass", "mid", "treble" };

juce::String joinDb(const std::vector<double>& v)
{
    juce::StringArray a;
    for (double d : v) a.add(juce::String(d, 3));
    return a.joinIntoString(",");
}

std::vector<double> splitDb(const juce::String& s)
{
    std::vector<double> v;
    for (auto& t : juce::StringArray::fromTokens(s, ",", "")) v.push_back(t.getDoubleValue());
    return v;
}

juce::File cacheFolderFor(const juce::File& f)
{
    const auto key = juce::String::toHexString((juce::int64) (f.getFullPathName() + "|" + juce::String(f.getLastModificationTime().toMilliseconds())
                                                                 + "|" + juce::String(f.getSize())).hashCode64());
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("AMPSURD amps").getChildFile(f.getFileNameWithoutExtension() + " " + key);
}
} // namespace

double knobFromText(const juce::String& raw)
{
    const auto t = raw.trim().toLowerCase();
    if (t == "min") return 0.0;
    if (t == "max") return 10.0;
    if (t.length() != 4 || !t.containsOnly("0123456789")) return -1.0;
    int h = t.substring(0, 2).getIntValue();
    const int m = t.substring(2).getIntValue();
    if (m >= 60 || h > 24) return -1.0;
    if (h < 7) h += 12;              // 0300 = 3 pm (a knob never points to 3 am)
    const double knob = (h - 7) + m / 60.0;
    return knob >= 0.0 && knob <= 10.0 ? knob : -1.0;
}

bool read(const juce::File& file, Profile& out, juce::String& error)
{
    juce::ZipFile zip(file);
    const auto* entry = zip.getEntry("amp.xml");
    if (entry == nullptr) { error = "Not an AMPSURD amp (no amp.xml inside): " + file.getFileName(); return false; }
    std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(*entry));
    const auto xml = in != nullptr ? juce::parseXML(in->readEntireStreamAsString()) : nullptr;
    if (xml == nullptr || !xml->hasTagName("AMPSURD_AMP")) { error = "The amp description inside is damaged: " + file.getFileName(); return false; }
    const auto dir = cacheFolderFor(file);
    if (!dir.getChildFile("amp.xml").existsAsFile())
    {
        dir.createDirectory();
        if (zip.uncompressTo(dir, true).failed()) { error = "Could not unpack " + file.getFileName(); return false; }
    }
    out = {};
    out.name = xml->getStringAttribute("name", file.getFileNameWithoutExtension());
    out.author = xml->getStringAttribute("author");
    for (auto* g : xml->getChildWithTagNameIterator("GAIN"))
    {
        const auto f = dir.getChildFile(g->getStringAttribute("file"));
        if (!f.existsAsFile()) { error = "A capture is missing inside the amp: " + g->getStringAttribute("file"); return false; }
        out.steps.push_back({ g->getDoubleAttribute("knob", 5.0), f });
    }
    if (out.steps.empty()) { error = "The amp has no gain captures."; return false; }
    std::sort(out.steps.begin(), out.steps.end(), [](const Profile::Step& a, const Profile::Step& b) { return a.knob < b.knob; });
    auto ts = std::make_shared<ToneStack>();
    const auto& grid = ToneSweep::gridHz();
    for (auto* t : xml->getChildWithTagNameIterator("TONE"))
    {
        const auto knob = t->getStringAttribute("knob").toLowerCase();
        int k = -1;
        for (int i = 0; i < 3; ++i) if (knob == kKnobNames[i]) k = i;
        if (k < 0) continue;
        for (auto* at : t->getChildWithTagNameIterator("AT"))
        {
            auto db = splitDb(at->getStringAttribute("db"));
            if (db.size() != grid.size()) continue; // another grid: not this version
            ts->knobs[(size_t) k].push_back({ at->getDoubleAttribute("pos"), std::move(db) });
        }
        std::sort(ts->knobs[(size_t) k].begin(), ts->knobs[(size_t) k].end(), [](auto& a, auto& b) { return a.first < b.first; });
    }
    out.tone = ts;
    return true;
}

bool build(const juce::File& folder, const juce::String& name, const juce::String& author, const juce::File& outFile,
           juce::String& report, juce::String& error)
{
    // gain captures: "...gain_HHMM..." in the name
    std::vector<std::pair<double, juce::File>> gains;
    for (const auto& f : folder.findChildFiles(juce::File::findFiles, false, "*.nam"))
    {
        const auto n = f.getFileNameWithoutExtension().toLowerCase();
        const int at = n.indexOf("gain_");
        const double knob = at >= 0 ? knobFromText(n.substring(at + 5, at + 9)) : -1.0;
        if (knob < 0.0) { report << "skipped (no gain_HHMM in the name): " << f.getFileName() << "\n"; continue; }
        gains.push_back({ knob, f });
    }
    std::sort(gains.begin(), gains.end(), [](auto& a, auto& b) { return a.first < b.first; });
    if (gains.empty()) { error = "No gain captures found: the .nam files need \"gain_HHMM\" in their names (e.g. G1_gain_0900.nam)."; return false; }

    // tone sweeps
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    auto analyse = [&](const juce::File& f, ToneSweep::Response& r) {
        std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(f));
        if (rd == nullptr) { report << "cannot read: " << f.getFileName() << "\n"; return false; }
        juce::AudioBuffer<float> b(1, (int) rd->lengthInSamples);
        rd->read(&b, 0, b.getNumSamples(), 0, true, false);
        std::vector<double> x((size_t) b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i) x[(size_t) i] = b.getSample(0, i);
        std::string e;
        if (!ToneSweep::analyse(x, rd->sampleRate, r, e)) { report << f.getFileName() << ": " << e << "\n"; return false; }
        if (r.usedQuiet) report << f.getFileName() << ": the loud sweep distorted - the quiet one was used\n";
        return true;
    };
    std::vector<ToneSweep::Response> refs;
    std::map<int, std::vector<std::pair<double, std::vector<double>>>> knobs;
    std::vector<std::pair<int, std::pair<double, juce::File>>> sweeps;
    for (const auto& f : folder.findChildFiles(juce::File::findFiles, false, "*.wav"))
    {
        const auto parts = juce::StringArray::fromTokens(f.getFileNameWithoutExtension().toLowerCase(), "_", "");
        if (parts.size() < 2 || !parts[0].startsWith("t")) continue;
        if (parts.contains("ref"))
        {
            ToneSweep::Response r;
            if (analyse(f, r)) refs.push_back(r);
            continue;
        }
        int k = -1;
        for (int i = 0; i < 3; ++i) if (parts[1] == kKnobNames[i]) k = i;
        const double pos = parts.size() >= 3 ? knobFromText(parts[2]) : -1.0;
        if (k < 0 || pos < 0.0) { report << "skipped (name not as in the recording sheet): " << f.getFileName() << "\n"; continue; }
        sweeps.push_back({ k, { pos, f } });
    }
    auto ts = std::make_shared<ToneStack>();
    const auto& grid = ToneSweep::gridHz();
    if (!sweeps.empty())
    {
        if (refs.empty()) { error = "Tone sweeps found but no all-noon reference (T00_ref_all_noon.wav)."; return false; }
        std::vector<double> ref(grid.size(), 0.0);
        for (auto& r : refs) for (size_t g = 0; g < grid.size(); ++g) ref[g] += r.db[g] / (double) refs.size();
        if (refs.size() >= 2)
        {
            double drift = 0.0;
            for (size_t g = 0; g < grid.size(); ++g)
                if (grid[g] >= 50 && grid[g] <= 12000) drift = std::max(drift, std::abs(refs.front().db[g] - refs.back().db[g]));
            report << "the two all-noon references differ by up to " << juce::String(drift, 2) << " dB"
                   << (drift > 1.0 ? " - something changed during the recordings, check the chain" : " (fine)") << "\n";
        }
        for (auto& s : sweeps)
        {
            ToneSweep::Response r;
            if (!analyse(s.second.second, r)) continue;
            std::vector<double> d(grid.size());
            for (size_t g = 0; g < grid.size(); ++g) d[g] = r.db[g] - ref[g];
            knobs[s.first].push_back({ s.second.first, d });
        }
        for (auto& kv : knobs)
        {
            auto& pts = kv.second;
            pts.push_back({ 5.0, std::vector<double>(grid.size(), 0.0) }); // noon = the reference
            std::sort(pts.begin(), pts.end(), [](auto& a, auto& b) { return a.first < b.first; });
            ts->knobs[(size_t) kv.first] = pts;
            report << kKnobNames[kv.first] << ": " << (int) pts.size() << " positions\n";
        }
    }
    else report << "no tone sweeps: the amp gets a GAIN knob only\n";

    // amp.xml + the captures
    juce::XmlElement xml("AMPSURD_AMP");
    xml.setAttribute("version", 1);
    xml.setAttribute("name", name);
    xml.setAttribute("author", author);
    auto* gridXml = xml.createNewChildElement("GRID");
    gridXml->setAttribute("hz", joinDb(grid));
    juce::ZipFile::Builder zip;
    for (auto& g : gains)
    {
        auto* e = xml.createNewChildElement("GAIN");
        e->setAttribute("knob", g.first);
        e->setAttribute("file", g.second.getFileName());
        zip.addFile(g.second, 9, g.second.getFileName());
        report << "GAIN " << juce::String(g.first, 2) << ": " << g.second.getFileName() << "\n";
    }
    for (int k = 0; k < 3; ++k)
    {
        if (ts->knobs[(size_t) k].empty()) continue;
        auto* t = xml.createNewChildElement("TONE");
        t->setAttribute("knob", kKnobNames[k]);
        for (auto& p : ts->knobs[(size_t) k])
        {
            auto* at = t->createNewChildElement("AT");
            at->setAttribute("pos", p.first);
            at->setAttribute("db", joinDb(p.second));
        }
    }
    const auto text = xml.toString();
    zip.addEntry(new juce::MemoryInputStream(text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 9, "amp.xml", juce::Time::getCurrentTime());
    outFile.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp(outFile);
    {
        juce::FileOutputStream os(tmp.getFile());
        if (!os.openedOk() || !zip.writeToStream(os, nullptr)) { error = "Could not write " + outFile.getFullPathName(); return false; }
    }
    if (!tmp.overwriteTargetFileWithTemporary()) { error = "Could not write " + outFile.getFullPathName(); return false; }
    return true;
}
} // namespace ampsurd::amp
