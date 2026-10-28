#include "CabIrMatrix.h"

#include <cmath>
#include <map>
#include <mutex>

#include "Theme.h"

namespace ampsurd::ui
{
using namespace colours;
using M = ampsurd::CabIrModel;

namespace
{
void label(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Colour c,
           juce::Justification j = juce::Justification::centredLeft, float size = 11.0f)
{
    g.setColour(c);
    g.setFont(Fonts::get().label(size));
    g.drawText(t, r, j, false);
}

juce::File irFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("AMPSURD").getChildFile("IRs");
}

// The original set's level: each IR at unit energy, then one common gain so the loudest peak is -1 dBFS.
double setGain(double rate)
{
    static std::mutex m;
    static std::map<double, double> cache;
    {
        std::lock_guard<std::mutex> l(m);
        if (auto it = cache.find(rate); it != cache.end()) return it->second;
    }
    double peak = 0.0;
    for (int row = 0; row < M::kRows; ++row)
        for (double mm = M::kMinMm; mm <= M::kMaxMm + 1e-9; mm += M::kStepMm)
        {
            const auto ir = M::make({ (double) row, mm }, rate);
            double e = 0.0, pk = 0.0;
            for (double v : ir) { e += v * v; pk = std::max(pk, std::abs(v)); }
            peak = std::max(peak, pk / std::sqrt(e));
        }
    const double g = std::pow(10.0, -1.0 / 20.0) / peak;
    std::lock_guard<std::mutex> l(m);
    cache[rate] = g;
    return g;
}
} // namespace

bool CabIrMatrix::writeIrFile(CabPoint p, double rate, const juce::File& f, juce::String& error)
{
    auto ir = M::make(p, rate);
    double e = 0.0;
    for (double v : ir) e += v * v;
    const double g = setGain(rate) / std::sqrt(e);
    juce::AudioBuffer<float> b(1, (int) ir.size());
    for (int i = 0; i < (int) ir.size(); ++i) b.setSample(0, i, (float) juce::jlimit(-1.0, 1.0, ir[(size_t) i] * g));
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream();
    if (os == nullptr) { error = "Cannot write " + f.getFullPathName(); return false; }
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(24));
    if (w == nullptr) { error = "Cannot write a WAV file"; return false; }
    if (!w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples())) { error = "Writing failed"; return false; }
    return true;
}

CabIrMatrix::CabIrMatrix(AmpsurdProcessor& p) : proc(p)
{
    setOpaque(true);
    for (auto* b : { &closeButton, &hoverButton, &snapButton, &saveButton, &exportButton })
    {
        addAndMakeVisible(*b);
        b->setWantsKeyboardFocus(false);
    }
    hoverButton.setClickingTogglesState(true);
    snapButton.setClickingTogglesState(true);
    hoverButton.setToggleState(true, juce::dontSendNotification);
    closeButton.onClick = [this] { close(); };
    hoverButton.onClick = [this] { if (!hoverButton.getToggleState()) endPreview(); };
    saveButton.onClick = [this] { saveChosen(); };
    exportButton.onClick = [this] { exportAll(); };
    closeButton.setTooltip("Back to the IR editor (Esc)");
    hoverButton.setTooltip("On: moving the mouse over the map lets you hear each point. Leaving the map returns to the "
                           "point you clicked. Off: only clicking / dragging changes the IR");
    snapButton.setTooltip("On: only the 144 points of the set (the dots). Off: anything in between, too");
    saveButton.setTooltip("Saves the chosen IR as a WAV into Documents / AMPSURD / IRs (to use it anywhere)");
    exportButton.setTooltip("Saves the whole set - 3 speakers x 3 mics x 16 positions - as WAVs into Documents / AMPSURD / IRs");
}

CabIrMatrix::~CabIrMatrix()
{
    stopTimer();
    proc.setIrPreview(false);
}

void CabIrMatrix::open(int s)
{
    slot = s;
    original = proc.getIrStatus(s);
    chosenValid = proc.getCabIrPoint(s, chosen);
    hovering = dragging = hasPending = false;
    status = chosenValid ? juce::String() : juce::String("Click or drag on the map to choose an IR (this replaces the amp's current IR).");
    setVisible(true);
    startTimerHz(30);
    repaint();
}

void CabIrMatrix::close()
{
    endPreview();
    timerCallback(); // send what is pending
    stopTimer();
    setVisible(false);
    slot = -1;
    if (onClose) onClose();
}

void CabIrMatrix::resized()
{
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(28);
    closeButton.setBounds(header.removeFromRight(80));
    r.removeFromTop(6);
    bottom = r.removeFromBottom(30);
    {
        auto b = bottom;
        hoverButton.setBounds(b.removeFromLeft(150));
        b.removeFromLeft(8);
        snapButton.setBounds(b.removeFromLeft(130));
        exportButton.setBounds(b.removeFromRight(140));
        b.removeFromRight(8);
        saveButton.setBounds(b.removeFromRight(90));
    }
    r.removeFromBottom(10);
    labels = r.removeFromLeft(150);
    axis = r.removeFromTop(34);
    labels.removeFromTop(34);
    map = r.reduced(0, 2);
    labels = labels.withHeight(map.getHeight()).withY(map.getY());
}

juce::Point<float> CabIrMatrix::positionOf(CabPoint p) const
{
    // cell centres: column c (0..15) at x of mm = 25 + 5c, row r at its row centre
    const float cw = (float) map.getWidth() / 16.0f, rh = (float) map.getHeight() / (float) M::kRows;
    const float x = (float) map.getX() + cw * (0.5f + (float) ((p.mm - M::kMinMm) / M::kStepMm));
    const float y = (float) map.getY() + rh * (0.5f + (float) p.row);
    return { x, y };
}

CabPoint CabIrMatrix::pointAt(juce::Point<int> pt) const
{
    const double cw = map.getWidth() / 16.0, rh = map.getHeight() / (double) M::kRows;
    CabPoint p;
    p.mm = M::kMinMm + M::kStepMm * ((pt.x - map.getX()) / cw - 0.5);
    p.row = (pt.y - map.getY()) / rh - 0.5;
    return snapped(M::clamp(p));
}

CabPoint CabIrMatrix::snapped(CabPoint p) const
{
    if (!snapButton.getToggleState()) return p;
    p.row = std::round(p.row);
    p.mm = M::kMinMm + M::kStepMm * std::round((p.mm - M::kMinMm) / M::kStepMm);
    return p;
}

void CabIrMatrix::request(CabPoint p)
{
    pending = p;
    hasPending = true;
}

void CabIrMatrix::timerCallback()
{
    if (slot < 0 || !hasPending) return;
    hasPending = false;
    CabPoint cur;
    if (proc.getCabIrPoint(slot, cur) && cur == pending && proc.getIrStatus(slot).state == AmpsurdProcessor::IrState::loaded) return;
    if (!proc.isIrOn(slot))
        if (auto* rp = proc.params.getParameter(AmpsurdProcessor::slotParamId(slot, "irOn"))) rp->setValueNotifyingHost(1.0f);
    proc.loadCabIr(slot, pending);
    sent = pending;
}

void CabIrMatrix::choose(CabPoint p)
{
    p = snapped(M::clamp(p));
    hovering = false;
    proc.setIrPreview(false);
    chosen = p;
    chosenValid = true;
    status.clear();
    request(p);
    repaint();
}

void CabIrMatrix::preview(CabPoint p)
{
    p = snapped(M::clamp(p));
    hover = p;
    if (!hovering) proc.setIrPreview(true); // hovering is listening, not a change: no undo steps
    hovering = true;
    request(p);
    repaint();
}

void CabIrMatrix::endPreview()
{
    if (!hovering) return;
    hovering = false;
    if (chosenValid) request(chosen);
    else
    {
        // back to the IR the amp had before (a file, or none)
        hasPending = false;
        if (original.state == AmpsurdProcessor::IrState::loaded && juce::File(original.path).existsAsFile())
            proc.loadIr(slot, juce::File(original.path));
        else if (original.state == AmpsurdProcessor::IrState::none)
            proc.removeIr(slot);
    }
    timerCallback();
    proc.setIrPreview(false);
    repaint();
}

void CabIrMatrix::mouseMove(const juce::MouseEvent& e)
{
    if (slot < 0 || dragging) return;
    if (map.contains(e.getPosition()))
    {
        if (hoverButton.getToggleState()) preview(pointAt(e.getPosition()));
        else { hover = pointAt(e.getPosition()); repaint(); }
    }
    else endPreview();
}

void CabIrMatrix::mouseExit(const juce::MouseEvent&) { endPreview(); }

void CabIrMatrix::mouseDown(const juce::MouseEvent& e)
{
    if (slot < 0 || !map.contains(e.getPosition())) return;
    dragging = true;
    choose(pointAt(e.getPosition()));
}

void CabIrMatrix::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging) choose(pointAt(e.getPosition()));
}

void CabIrMatrix::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
    timerCallback();
}

void CabIrMatrix::saveChosen()
{
    if (!chosenValid) { setStatus("Choose a point first (click on the map)."); return; }
    const double rate = proc.getSampleRateForUi() > 0.0 ? proc.getSampleRateForUi() : 48000.0;
    const auto f = irFolder().getNonexistentChildFile(juce::String(M::fileName(chosen)), ".wav", false);
    setStatus("Saving...");
    juce::Thread::launch([sp = juce::Component::SafePointer<CabIrMatrix>(this), p = chosen, rate, f] {
        juce::String err;
        const bool ok = writeIrFile(p, rate, f, err);
        juce::MessageManager::callAsync([sp, ok, err, f] {
            if (sp == nullptr) return;
            sp->setStatus(ok ? "Saved: " + f.getFileName() + "  (Documents / AMPSURD / IRs)" : err);
            if (ok) f.revealToUser();
        });
    });
}

void CabIrMatrix::exportAll()
{
    const double rate = proc.getSampleRateForUi() > 0.0 ? proc.getSampleRateForUi() : 48000.0;
    const auto rateTag = std::abs(rate - 44100.0) < 1.0 ? juce::String("44.1k") : juce::String(juce::roundToInt(rate / 1000.0)) + "k";
    const auto folder = irFolder().getChildFile("AMPSURD IRs").getChildFile(rateTag);
    setStatus("Exporting 144 IRs...");
    exportButton.setEnabled(false);
    juce::Thread::launch([sp = juce::Component::SafePointer<CabIrMatrix>(this), rate, folder] {
        int written = 0;
        juce::String err;
        for (int row = 0; row < M::kRows && err.isEmpty(); ++row)
            for (double mm = M::kMinMm; mm <= M::kMaxMm + 1e-9 && err.isEmpty(); mm += M::kStepMm)
            {
                const CabPoint p { (double) row, mm };
                const auto sub = folder.getChildFile(juce::String(M::speakerFileTag(M::speakerOfRow(row))) + "-" + M::micFileTag(M::micOfRow(row)));
                if (writeIrFile(p, rate, sub.getChildFile(juce::String(M::fileName(p)) + ".wav"), err)) ++written;
            }
        juce::MessageManager::callAsync([sp, written, err, folder] {
            if (sp == nullptr) return;
            sp->exportButton.setEnabled(true);
            sp->setStatus(err.isEmpty() ? juce::String(written) + " IRs saved in Documents / AMPSURD / IRs / AMPSURD IRs / " + folder.getFileName() : err);
            if (err.isEmpty()) folder.revealToUser();
        });
    });
}

void CabIrMatrix::paint(juce::Graphics& g)
{
    g.fillAll(background);
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(sectionBorder * 0.5f), 3.0f, sectionBorder);
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(28);
    label(g, "AMPSURD IRs" + (slot >= 0 ? "  /  AMP " + juce::String(slot + 1) : juce::String()), header.removeFromLeft(190), accent);
    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawText("Calculated 4x12 cabinet IRs. Click, drag or hover anywhere - between the dots you hear the sound in between.",
               header.withTrimmedRight(90), juce::Justification::centredLeft, true);

    // axis: mic position
    const float cw = (float) map.getWidth() / 16.0f, rh = (float) map.getHeight() / (float) M::kRows;
    g.setFont(Fonts::get().regular(11.5f));
    for (int c = 0; c < 16; ++c)
    {
        const int mm = 25 + 5 * c;
        const float x = (float) map.getX() + cw * (c + 0.5f);
        g.setColour(c % 3 == 0 || c == 15 ? textDim : textFaint);
        g.drawText(juce::String(mm), juce::Rectangle<float>(x - 20.0f, (float) axis.getY() + 16.0f, 40.0f, 16.0f), juce::Justification::centred, false);
    }
    label(g, "MIC POSITION (MM FROM THE CENTRE)", axis.withHeight(14), textDim, juce::Justification::centred, 10.5f);
    label(g, "CENTRE / CAP EDGE", axis.withHeight(14).withWidth(160), textFaint, juce::Justification::centredLeft, 10.0f);
    label(g, "CONE", axis.withHeight(14), textFaint, juce::Justification::centredRight, 10.0f);
    label(g, "SPEAKER / MIC", labels.withY(axis.getY()).withHeight(30), textDim, juce::Justification::bottomLeft, 10.5f);

    // rows: speaker groups and mics
    for (int row = 0; row < M::kRows; ++row)
    {
        const float y = (float) map.getY() + rh * row;
        if (row % M::kMics == 0)
        {
            g.setColour(line);
            g.drawHorizontalLine((int) y, (float) labels.getX(), (float) map.getRight());
            label(g, M::speakerName(M::speakerOfRow(row)), juce::Rectangle<int>(labels.getX(), (int) y, 70, (int) (rh * M::kMics)), text,
                  juce::Justification::centredLeft, 12.0f);
        }
        g.setColour(textDim);
        g.setFont(Fonts::get().regular(12.0f));
        g.drawText(M::micName(M::micOfRow(row)), juce::Rectangle<float>((float) labels.getX() + 72.0f, y, 76.0f, rh), juce::Justification::centredLeft, false);
    }
    g.setColour(line);
    g.drawHorizontalLine(map.getBottom(), (float) labels.getX(), (float) map.getRight());
    g.drawRect(map);

    // the set's 144 points
    for (int row = 0; row < M::kRows; ++row)
        for (int c = 0; c < 16; ++c)
        {
            const auto pt = positionOf({ (double) row, 25.0 + 5.0 * c });
            g.setColour(lineStrong);
            g.fillEllipse(pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f);
        }

    // hovered point (ring) and the chosen one (filled, with cross lines)
    if (hovering || (isMouseOver(true) && map.contains(getMouseXYRelative())))
    {
        const auto h = positionOf(hover);
        g.setColour(textDim);
        g.drawEllipse(h.x - 8.0f, h.y - 8.0f, 16.0f, 16.0f, 1.5f);
    }
    if (chosenValid)
    {
        const auto c = positionOf(chosen);
        g.setColour(text.withAlpha(0.25f));
        g.drawHorizontalLine((int) c.y, (float) map.getX(), (float) map.getRight());
        g.drawVerticalLine((int) c.x, (float) map.getY(), (float) map.getBottom());
        g.setColour(accent);
        g.fillEllipse(c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
        g.setColour(text);
        g.drawEllipse(c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f, 1.5f);
    }

    // what is heard, and messages
    auto info = bottom.withTrimmedLeft(300).withTrimmedRight(250);
    g.setFont(Fonts::get().regular(13.0f));
    g.setColour(text);
    const juce::String now = hovering ? "Listening: " + juce::String(M::describe(hover))
                           : chosenValid ? "Chosen: " + juce::String(M::describe(chosen)) : juce::String();
    if (status.isNotEmpty())
    {
        g.setColour(textDim);
        g.drawFittedText(status, info, juce::Justification::centredLeft, 2, 0.85f);
    }
    else g.drawFittedText(now, info, juce::Justification::centredLeft, 1, 0.85f);
}
} // namespace ampsurd::ui
