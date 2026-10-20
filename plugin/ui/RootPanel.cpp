#include "RootPanel.h"

#if AMPSURD_ROOT

#include <cmath>

#include "Theme.h"

namespace ampsurd::ui
{
using namespace colours;

namespace
{
void label(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Colour c,
           juce::Justification j = juce::Justification::centredLeft, float size = 11.0f)
{
    g.setColour(c);
    g.setFont(Fonts::get().label(size));
    g.drawText(t, r, j, false);
}

const char* modeText(int m)
{
    switch (m)
    {
        case 0: return "PARALLEL: a clean, compressed low layer from your DI guitar, saturated asymmetrically so a power chord adds its "
                       "own difference tone one octave below the root. Tight, under the amps.";
        case 1: return "POST: the same layer, taken from the amps' own output - thickens the low end they already have.";
        default: return "SUB (experimental): PARALLEL plus a tone one octave below, from a divider. Best on single low notes; "
                        "on chords it can jump between notes.";
    }
}

constexpr double kMinHz = 20.0, kMaxHz = 500.0, kMinDb = -30.0, kMaxDb = 18.0;
} // namespace

RootPanel::Row RootPanel::makeRow(const juce::String& text, const juce::String& paramId, const juce::String& tip)
{
    Row r;
    r.label = text;
    r.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    r.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 74, 22);
    r.slider->setTextBoxIsEditable(true);
    r.slider->setTooltip(tip);
    addAndMakeVisible(*r.slider);
    r.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params, paramId, *r.slider);
    return r;
}

RootPanel::RootPanel(AmpsurdProcessor& p) : proc(p)
{
    onButton.setClickingTogglesState(true);
    invertButton.setClickingTogglesState(true);
    for (auto* b : { &onButton, &invertButton, &closeButton })
        addAndMakeVisible(*b);
    onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params, "rootOn", onButton);
    invertAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params, "rootInvert", invertButton);
    onButton.setTooltip("ROOT on / off (experimental). Off: AMPSURD sounds exactly as without it");
    invertButton.setTooltip("Flips the layer's polarity - try it if turning ROOT up makes the low end thinner instead of bigger");
    closeButton.setTooltip("Close ROOT (it keeps running if it is ON)");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    const char* tips[3] = { "PARALLEL: layer from the clean DI guitar (recommended)", "POST: layer from the amps' output",
                            "SUB: adds an octave-down tone (experimental, single notes work best)" };
    for (int m = 0; m < 3; ++m)
    {
        modeButtons[(size_t) m].setTooltip(tips[m]);
        modeButtons[(size_t) m].onClick = [this, m] {
            if (auto* rp = proc.params.getParameter("rootMode")) rp->setValueNotifyingHost(rp->convertTo0to1((float) m));
        };
        addAndMakeVisible(modeButtons[(size_t) m]);
    }
    rows[0] = makeRow("ROOT", "rootLevel", "ROOT: how loud the low layer is (0 dB = as loud as the low band of the guitar)");
    rows[1] = makeRow("FREQ", "rootFreq", "FREQ: the top of the low band the layer is made from (lower = deeper, higher = more punch)");
    rows[2] = makeRow("GROWL", "rootGrowl", "GROWL: 0 = clean low band, 100 = saturated: difference tones under power chords (loudness kept)");
    rows[3] = makeRow("TIGHT", "rootTight", "TIGHT: removes the rumble below this frequency");
    rows[4] = makeRow("COMP", "rootComp", "COMP: evens out the layer (palm mutes stay as big as open chords)");
    eqRows[0] = makeRow("SUB 50", "rootSub", "BASS EQ: low shelf at 50 Hz");
    eqRows[1] = makeRow("LOW 100", "rootLow", "BASS EQ: 100 Hz");
    eqRows[2] = makeRow("LOW-MID 250", "rootLowMid", "BASS EQ: 250 Hz");
    for (auto& r : eqRows)
        r.slider->setDoubleClickReturnValue(true, 0.0);
    rows[0].slider->setDoubleClickReturnValue(true, 0.0);
}

juce::Rectangle<int> RootPanel::graphArea() const
{
    auto r = getLocalBounds().reduced(16, 12);
    r.removeFromTop(36);
    auto right = r.removeFromRight(360);
    return right.removeFromTop(right.getHeight() - 3 * 34 - 8).withTrimmedBottom(4);
}

void RootPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(26);
    closeButton.setBounds(header.removeFromRight(72));
    r.removeFromTop(10);

    auto left = r.removeFromLeft(300);
    auto b = left.removeFromTop(30);
    onButton.setBounds(b.removeFromLeft(64));
    b.removeFromLeft(10);
    invertButton.setBounds(b.removeFromLeft(96));
    left.removeFromTop(10);
    auto m = left.removeFromTop(30);
    for (auto& mb : modeButtons)
    {
        mb.setBounds(m.removeFromLeft(92));
        m.removeFromLeft(8);
    }

    r.removeFromLeft(24);
    auto right = r.removeFromRight(360);
    r.removeFromRight(24);
    for (auto& row : rows)
    {
        auto line = r.removeFromTop(34);
        line.removeFromLeft(70);
        row.slider->setBounds(line.withSizeKeepingCentre(line.getWidth(), 24));
    }
    right.removeFromTop(right.getHeight() - 3 * 34);
    for (auto& row : eqRows)
    {
        auto line = right.removeFromTop(34);
        line.removeFromLeft(96);
        row.slider->setBounds(line.withSizeKeepingCentre(line.getWidth(), 24));
    }
}

void RootPanel::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(sectionBorder * 0.5f), 3.0f, sectionBorder);
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(26);
    label(g, "ROOT", header.removeFromLeft(54), accent);
    {
        auto tag = header.removeFromLeft(96).withSizeKeepingCentre(92, 18);
        g.setColour(textFaint);
        g.drawRoundedRectangle(tag.toFloat(), 2.0f, 1.0f);
        label(g, "EXPERIMENTAL", tag, textDim, juce::Justification::centred, 9.5f);
    }
    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawText("Makes the lowest note of a power chord deeper - a low layer under the amps, before the gate. No pitch shifter.",
               header.withTrimmedRight(90).withTrimmedLeft(10), juce::Justification::centredLeft, true);
    r.removeFromTop(10);

    // left: what the mode does
    auto left = r.removeFromLeft(300);
    left.removeFromTop(80);
    g.setColour(proc.isRootOn() ? textDim : textFaint);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawFittedText(modeText(shownMode < 0 ? 0 : shownMode), left.removeFromTop(90), juce::Justification::topLeft, 6, 0.9f);

    // middle: labels of the rows
    r.removeFromLeft(24);
    auto right = r.removeFromRight(360);
    r.removeFromRight(24);
    for (auto& row : rows)
        label(g, row.label, r.removeFromTop(34).removeFromLeft(66), proc.isRootOn() ? text : textFaint);

    // right: curve of the layer (filters + BASS EQ + level) and meter
    const auto ga = graphArea();
    g.setColour(grid);
    auto xFor = [&](double hz) { return (float) ga.getX() + (float) (std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz)) * (float) ga.getWidth(); };
    auto yFor = [&](double db) { return (float) ga.getBottom() - (float) ((db - kMinDb) / (kMaxDb - kMinDb)) * (float) ga.getHeight(); };
    for (double hz : { 30.0, 50.0, 100.0, 200.0 })
    {
        g.drawVerticalLine((int) xFor(hz), (float) ga.getY(), (float) ga.getBottom());
        label(g, juce::String((int) hz) + (hz >= 200.0 ? " Hz" : ""), { (int) xFor(hz) + 3, ga.getBottom() - 14, 44, 12 }, textFaint,
              juce::Justification::centredLeft, 9.5f);
    }
    g.drawHorizontalLine((int) yFor(0.0), (float) ga.getX(), (float) ga.getRight());
    g.setColour(line);
    g.drawRect(ga);
    juce::Path p;
    const auto s = shownSettings;
    for (int x = 0; x <= ga.getWidth(); x += 2)
    {
        const double hz = kMinHz * std::pow(kMaxHz / kMinHz, (double) x / (double) ga.getWidth());
        const double db = juce::jlimit(kMinDb, kMaxDb, ampsurd::RootEnhancer::responseDb(s, hz, 48000.0));
        const float px = (float) (ga.getX() + x), py = yFor(db);
        if (x == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
    }
    g.setColour(proc.isRootOn() ? text : textFaint);
    g.strokePath(p, juce::PathStrokeType(1.6f));
    label(g, "LAYER", { ga.getX() + 6, ga.getY() + 4, 60, 12 }, textFaint, juce::Justification::centredLeft, 9.5f);
    // meter: layer level (dBFS) and the compressor's gain reduction
    {
        const float db = meter > 1e-6f ? 20.0f * std::log10(meter) : -90.0f;
        const auto mr = juce::Rectangle<int>(ga.getRight() - 120, ga.getY() + 4, 114, 12);
        label(g, proc.isRootOn() ? juce::String(db <= -89.0f ? "-inf" : juce::String(db, 1)) + " dB" + (grDb > 0.5f ? "  COMP -" + juce::String(grDb, 1) : "")
                                 : juce::String("OFF"),
              mr, textDim, juce::Justification::centredRight, 9.5f);
    }

    right.removeFromTop(right.getHeight() - 3 * 34);
    for (auto& row : eqRows)
        label(g, row.label, right.removeFromTop(34).removeFromLeft(92), proc.isRootOn() ? text : textFaint);
}

void RootPanel::refresh()
{
    const auto s = proc.readRoot();
    const int mode = (int) s.mode;
    if (mode != shownMode)
    {
        shownMode = mode;
        for (int m = 0; m < 3; ++m)
            modeButtons[(size_t) m].setToggleState(m == mode, juce::dontSendNotification);
        repaint();
    }
    const float pk = proc.getAndResetRootLayerPeak(), gr = proc.getAndResetRootGainReductionDb();
    meter = juce::jmax(pk, meter * 0.85f);
    grDb = juce::jmax(gr, grDb * 0.85f);
    auto differs = [](double a, double b) { return !juce::exactlyEqual(a, b); };
    const bool changed = differs(s.freqHz, shownSettings.freqHz) || differs(s.tightHz, shownSettings.tightHz)
                         || differs(s.subDb, shownSettings.subDb) || differs(s.lowDb, shownSettings.lowDb)
                         || differs(s.lowMidDb, shownSettings.lowMidDb) || differs(s.levelDb, shownSettings.levelDb) || s.on != shownSettings.on;
    shownSettings = s;
    if (changed) repaint();
    else repaint(graphArea());
}
} // namespace ampsurd::ui
#endif
