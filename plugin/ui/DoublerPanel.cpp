#include "DoublerPanel.h"

#if AMPSURD_DOUBLER

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

const char* voicesText(int n)
{
    switch (n)
    {
        case 1: return "1 DOUBLE: your guitar on the left, a second take on the right - the classic double-tracked rhythm guitar.";
        case 2: return "2 DOUBLES: your guitar in the centre, a different second take on each side. Wide and big.";
        default: return "3 DOUBLES: a wall of four takes - two on the left, two on the right.";
    }
}

// pan positions as the module places them (Doubler.cpp): guitar, doubles 1..3
float panOf(int voices, int who, float w)
{
    if (who == 0) return voices == 1 ? -w : (voices == 3 ? -0.25f * w : 0.0f);
    const int i = who - 1;
    if (voices == 1) return i == 0 ? w : 0.0f;
    return i == 0 ? -w : (i == 1 ? w : 0.4f * w);
}
} // namespace

DoublerPanel::Row DoublerPanel::makeRow(const juce::String& text, const juce::String& paramId, const juce::String& tip)
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

DoublerPanel::DoublerPanel(AmpsurdProcessor& p) : proc(p)
{
    onButton.setClickingTogglesState(true);
    for (auto* b : { &onButton, &closeButton })
        addAndMakeVisible(*b);
    onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params, "dblOn", onButton);
    onButton.setTooltip("DOUBLER on / off (experimental). Off: AMPSURD sounds exactly as without it");
    closeButton.setTooltip("Close DOUBLER (it keeps running if it is ON)");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    for (int v = 0; v < 3; ++v)
    {
        voiceButtons[(size_t) v].setTooltip(juce::String(v + 1) + (v == 0 ? " double" : " doubles") + " (second takes) of your guitar");
        voiceButtons[(size_t) v].onClick = [this, v] {
            if (auto* rp = proc.params.getParameter("dblVoices")) rp->setValueNotifyingHost(rp->convertTo0to1((float) v));
        };
        addAndMakeVisible(voiceButtons[(size_t) v]);
    }
    rows[0] = makeRow("TIGHT", "dblTight", "TIGHT: 100 % = a very tight player (8-13 ms, little drift); 0 % = loose (up to 33 ms, wanders more)");
    rows[1] = makeRow("LEVEL", "dblLevel", "LEVEL: each double vs. your guitar. The whole stays as loud as your guitar alone");
    rows[2] = makeRow("WIDTH", "dblWidth", "WIDTH: 100 % = hard left / right, 0 % = everything in the centre");
    rows[3] = makeRow("LOW MONO", "dblLowMono", "LOW MONO: below this only your guitar plays (tight, centred lows). OFF = the doubles keep their lows");
    rows[4] = makeRow("CHARACTER", "dblCharacter", "CHARACTER: how different the takes sound - tone and pick strength per note");
    rows[1].slider->setDoubleClickReturnValue(true, -2.0);
}

juce::Rectangle<int> DoublerPanel::pictureArea() const
{
    auto r = getLocalBounds().reduced(16, 12);
    r.removeFromTop(36);
    return r.removeFromRight(360).withTrimmedBottom(4);
}

void DoublerPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(26);
    closeButton.setBounds(header.removeFromRight(72));
    r.removeFromTop(10);

    auto left = r.removeFromLeft(300);
    auto b = left.removeFromTop(30);
    onButton.setBounds(b.removeFromLeft(64));
    left.removeFromTop(10);
    auto m = left.removeFromTop(30);
    m.removeFromLeft(80); // "DOUBLES" label
    for (auto& vb : voiceButtons)
    {
        vb.setBounds(m.removeFromLeft(44));
        m.removeFromLeft(8);
    }

    r.removeFromLeft(24);
    r.removeFromRight(360);
    r.removeFromRight(24);
    for (auto& row : rows)
    {
        auto line = r.removeFromTop(34);
        line.removeFromLeft(86);
        row.slider->setBounds(line.withSizeKeepingCentre(line.getWidth(), 24));
    }
}

void DoublerPanel::paint(juce::Graphics& g)
{
    const bool on = proc.isDoublerOn();
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(sectionBorder * 0.5f), 3.0f, sectionBorder);
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(26);
    label(g, "DOUBLER", header.removeFromLeft(84), accent);
    {
        auto tag = header.removeFromLeft(96).withSizeKeepingCentre(92, 18);
        g.setColour(textFaint);
        g.drawRoundedRectangle(tag.toFloat(), 2.0f, 1.0f);
        label(g, "EXPERIMENTAL", tag, textDim, juce::Justification::centred, 9.5f);
    }
    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawText("Double tracking from one guitar: second takes with their own timing, pitch and tone. No extra latency.",
               header.withTrimmedRight(90).withTrimmedLeft(10), juce::Justification::centredLeft, true);
    r.removeFromTop(10);

    auto left = r.removeFromLeft(300);
    left.removeFromTop(40);
    label(g, "DOUBLES", left.removeFromTop(30).removeFromLeft(78), on ? text : textFaint);
    left.removeFromTop(12);
    g.setColour(on ? textDim : textFaint);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawFittedText(voicesText(shownVoices < 1 ? 2 : shownVoices), left.removeFromTop(80), juce::Justification::topLeft, 5, 0.9f);

    r.removeFromLeft(24);
    r.removeFromRight(360);
    r.removeFromRight(24);
    for (auto& row : rows)
        label(g, row.label, r.removeFromTop(34).removeFromLeft(84), on ? text : textFaint);

    // stereo picture: x = left .. right, y = how late (0 .. 40 ms)
    const auto pa = pictureArea();
    g.setColour(grid);
    g.drawRect(pa);
    const int cx = pa.getCentreX();
    g.drawVerticalLine(cx, (float) pa.getY(), (float) pa.getBottom());
    auto yFor = [&](float ms) { return (float) pa.getY() + 22.0f + (float) (pa.getHeight() - 34) * juce::jlimit(0.0f, 40.0f, ms) / 40.0f; };
    for (float ms : { 10.0f, 20.0f, 30.0f })
    {
        g.setColour(grid);
        g.drawHorizontalLine((int) yFor(ms), (float) pa.getX(), (float) pa.getRight());
        label(g, juce::String((int) ms) + " ms", { cx + 4, (int) yFor(ms) - 13, 50, 12 }, textFaint, juce::Justification::centredLeft, 9.5f);
    }
    label(g, "L", { pa.getX() + 4, pa.getY() + 4, 20, 12 }, textFaint, juce::Justification::centredLeft, 9.5f);
    label(g, "R", { pa.getRight() - 24, pa.getY() + 4, 20, 12 }, textFaint, juce::Justification::centredRight, 9.5f);
    const int voices = shownVoices < 1 ? 2 : shownVoices;
    const float w = (float) (proc.params.getRawParameterValue("dblWidth")->load() / 100.0);
    auto xFor = [&](float pan) { return (float) cx + pan * (float) (pa.getWidth() / 2 - 30); };
    auto dot = [&](float x, float y, juce::Colour c, const juce::String& t) {
        g.setColour(c);
        g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);
        label(g, t, { (int) x - 40, (int) y + 7, 80, 12 }, c, juce::Justification::centred, 9.5f);
    };
    dot(xFor(panOf(voices, 0, w)), yFor(0.0f), on ? accent : textFaint, "GUITAR");
    for (int i = 0; i < voices; ++i)
    {
        const float ms = on && shownDelay[(size_t) i] > 0.0f ? shownDelay[(size_t) i] : 12.0f + 6.0f * (float) i;
        dot(xFor(panOf(voices, i + 1, w)), yFor(ms), on ? text : textFaint, "TAKE " + juce::String(i + 2));
    }
}

void DoublerPanel::refresh()
{
    const int voices = 1 + juce::jlimit(0, 2, (int) proc.params.getRawParameterValue("dblVoices")->load());
    const bool on = proc.isDoublerOn();
    bool changed = voices != shownVoices || on != shownOn;
    if (voices != shownVoices)
        for (int v = 0; v < 3; ++v)
            voiceButtons[(size_t) v].setToggleState(v + 1 == voices, juce::dontSendNotification);
    shownVoices = voices;
    shownOn = on;
    for (int i = 0; i < 3; ++i)
    {
        const float d = proc.getDoublerDelayMs(i);
        if (std::abs(d - shownDelay[(size_t) i]) > 0.05f) { shownDelay[(size_t) i] = d; changed = true; }
    }
    if (changed) repaint();
}
} // namespace ampsurd::ui
#endif
