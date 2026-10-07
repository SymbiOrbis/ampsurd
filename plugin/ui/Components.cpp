#include "Components.h"

#include "BinaryData.h"

#include <limits>

namespace ampsurd::ui
{
using namespace colours;

namespace
{
juce::RangedAudioParameter* param(AmpsurdProcessor& p, const juce::String& id) { return p.params.getParameter(id); }

float paramValue(AmpsurdProcessor& p, const juce::String& id)
{
    if (auto* v = p.params.getRawParameterValue(id)) return v->load();
    return 0.0f;
}

void setParamValue(AmpsurdProcessor& p, const juce::String& id, float value)
{
    if (auto* rp = param(p, id)) rp->setValueNotifyingHost(rp->convertTo0to1(value));
}

bool isLoadedState(AmpsurdProcessor::SlotState s)
{
    return s == AmpsurdProcessor::SlotState::loaded || s == AmpsurdProcessor::SlotState::loading;
}

void drawLabel(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Colour c,
               juce::Justification j = juce::Justification::centredLeft, float size = 11.0f)
{
    g.setColour(c);
    g.setFont(Fonts::get().label(size));
    g.drawText(t, r, j, false);
}
} // namespace

// =============================================================================================
// FilenameLayout
// =============================================================================================
juce::StringArray FilenameLayout::wrap(const juce::String& text, const juce::Font& font, float maxWidth, int maxLines,
                                       bool& truncated)
{
    truncated = false;
    auto width = [&](const juce::String& s) { return juce::GlyphArrangement::getStringWidth(font, s); };

    // tokens end after a natural break character; the characters themselves are kept unchanged
    juce::StringArray tokens;
    juce::String cur;
    for (auto c : text)
    {
        cur += juce::String::charToString(c);
        if (c == '_' || c == '-' || c == ' ' || c == '.' || c == '(' || c == ')')
        {
            tokens.add(cur);
            cur.clear();
        }
    }
    if (cur.isNotEmpty()) tokens.add(cur);

    juce::StringArray lines;
    juce::String line;
    auto pushLine = [&] { lines.add(line.trimEnd()); line.clear(); };

    for (auto tok : tokens)
    {
        if (width(line + tok) <= maxWidth || line.isEmpty())
        {
            if (line.isEmpty() && width(tok) > maxWidth)
            {
                // a single token wider than the line: break between characters
                for (auto c : tok)
                {
                    const auto ch = juce::String::charToString(c);
                    if (width(line + ch) > maxWidth && line.isNotEmpty()) pushLine();
                    line += ch;
                }
            }
            else
                line += tok;
        }
        else
        {
            pushLine();
            if (width(tok) > maxWidth)
            {
                for (auto c : tok)
                {
                    const auto ch = juce::String::charToString(c);
                    if (width(line + ch) > maxWidth && line.isNotEmpty()) pushLine();
                    line += ch;
                }
            }
            else
                line = tok;
        }
    }
    if (line.isNotEmpty()) pushLine();

    if (lines.size() > maxLines)
    {
        truncated = true;
        lines.removeRange(maxLines, lines.size() - maxLines);
        auto last = lines[maxLines - 1];
        const juce::String ell = juce::String::charToString((juce::juce_wchar) 0x2026);
        while (last.isNotEmpty() && width(last + ell) > maxWidth)
            last = last.dropLastCharacters(1);
        lines.set(maxLines - 1, last + ell);
    }
    return lines;
}

void FilenameLayout::draw(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto font = Fonts::get().medium(kFontSize);
    const int maxLines = juce::jmin(kMaxLines, juce::jmax(1, (int) (area.getHeight() / kLineHeight)));
    bool truncated = false;
    const auto lines = wrap(text, font, area.getWidth(), maxLines, truncated);
    const float blockH = (float) lines.size() * kLineHeight;
    float y = area.getCentreY() - blockH * 0.5f;
    g.setFont(font);
    g.setColour(colour);
    for (const auto& l : lines)
    {
        g.drawText(l, juce::Rectangle<float>(area.getX(), y, area.getWidth(), kLineHeight), juce::Justification::centred, false);
        y += kLineHeight;
    }
}

// =============================================================================================
// MixFader
// =============================================================================================
MixFader::MixFader(AmpsurdProcessor& p, int s) : proc(p), slot(s) {}

juce::Rectangle<float> MixFader::trackArea() const
{
    return getLocalBounds().toFloat().reduced(0.0f, 8.0f);
}

float MixFader::valueFromY(float y) const
{
    const auto t = trackArea();
    return juce::jlimit(0.0f, 100.0f, (t.getBottom() - y) / t.getHeight() * 100.0f);
}

void MixFader::paint(juce::Graphics& g)
{
    const bool enabled = isEnabled();
    const auto t = trackArea();
    const float cx = t.getCentreX();
    const float v = enabled ? paramValue(proc, AmpsurdProcessor::slotParamId(slot, "mix")) : 0.0f;
    const float y = t.getBottom() - v / 100.0f * t.getHeight();

    // scale ticks every 25 %
    g.setColour(line);
    for (int i = 0; i <= 4; ++i)
    {
        const float ty = t.getBottom() - (float) i * t.getHeight() / 4.0f;
        g.fillRect(cx - 9.0f, ty - 0.5f, 4.0f, 1.0f);
        g.fillRect(cx + 5.0f, ty - 0.5f, 4.0f, 1.0f);
    }
    g.fillRect(cx - 0.5f, t.getY(), 1.0f, t.getHeight());
    if (!enabled)
        return;
    g.setColour(textDim);
    g.fillRect(cx - 1.0f, y, 2.0f, t.getBottom() - y);
    g.setColour(text);
    g.fillRoundedRectangle(cx - 12.0f, y - 4.0f, 24.0f, 8.0f, 1.5f);
    g.setColour(background);
    g.fillRect(cx - 8.0f, y - 0.5f, 16.0f, 1.0f);
}

void MixFader::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled()) return;
    dragging = true;
    proc.beginMixGesture(slot);
    proc.setMixLinked(slot, valueFromY((float) e.position.y));
    repaint();
}

void MixFader::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging) return;
    proc.setMixLinked(slot, valueFromY((float) e.position.y));
}

void MixFader::mouseUp(const juce::MouseEvent&)
{
    if (!dragging) return;
    dragging = false;
    proc.endMixGesture(slot);
}

void MixFader::mouseDoubleClick(const juce::MouseEvent&)
{
    if (!isEnabled()) return;
    // equal share of all loaded slots
    int loaded = 0;
    for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
        if (isLoadedState(proc.getSlotStatus(s).state)) ++loaded;
    proc.beginMixGesture(slot);
    proc.setMixLinked(slot, 100.0f / (float) juce::jmax(1, loaded));
    proc.endMixGesture(slot);
}

void MixFader::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (!isEnabled()) return;
    const float v = paramValue(proc, AmpsurdProcessor::slotParamId(slot, "mix"));
    proc.beginMixGesture(slot);
    proc.setMixLinked(slot, v + (w.deltaY > 0 ? 1.0f : -1.0f));
    proc.endMixGesture(slot);
}

// =============================================================================================
// SlotComponent
// =============================================================================================
SlotComponent::SlotComponent(AmpsurdProcessor& p, int s) : proc(p), slot(s), fader(p, s)
{
    for (auto* b : { &loadButton, &editButton, &soloButton, &muteButton })
        addAndMakeVisible(*b);
    addAndMakeVisible(fader);
    soloButton.setClickingTogglesState(true);
    muteButton.setClickingTogglesState(true);
    soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params, AmpsurdProcessor::slotParamId(s, "solo"), soloButton);
    muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params, AmpsurdProcessor::slotParamId(s, "mute"), muteButton);
    loadButton.onClick = [this] { chooseFile(); };
    editButton.onClick = [this] { if (onEditClicked) onEditClicked(slot); };
    loadButton.setTooltip("Load a .nam capture (or drop a file onto this slot)");
    soloButton.setTooltip("Hear only the soloed amps");
    muteButton.setTooltip("Silence this amp (its mix share is kept)");
    editButton.setTooltip("Open EQ and alignment for this amp");
}

SlotComponent::~SlotComponent() = default;

juce::Rectangle<int> SlotComponent::nameArea() const
{
    return { 14, 40, getWidth() - 14 - 14 - 44, getHeight() - 40 - 92 };
}

void SlotComponent::resized()
{
    auto r = getLocalBounds().reduced(14);
    auto buttons = r.removeFromBottom(68);
    auto row1 = buttons.removeFromTop(30), row2 = buttons.removeFromBottom(30);
    const int bw = (row1.getWidth() - 8) / 2;
    loadButton.setBounds(row1.removeFromLeft(bw));
    editButton.setBounds(row1.removeFromRight(bw));
    soloButton.setBounds(row2.removeFromLeft(bw));
    muteButton.setBounds(row2.removeFromRight(bw));
    r.removeFromBottom(12);
    fader.setBounds(r.removeFromRight(40).withTrimmedTop(26));
}

void SlotComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    if (selected)
    {
        g.setColour(raised);
        g.fillRoundedRectangle(bounds, 3.0f);
    }
    g.setColour(selected ? lineStrong : (dragHover ? textDim : line));
    g.drawRoundedRectangle(bounds, 3.0f, selected ? 1.5f : 1.0f);

    // slot number + state
    drawLabel(g, juce::String(slot + 1), { 14, 10, 40, 18 }, textFaint);
    juce::String stateText;
    switch (status.state)
    {
        case AmpsurdProcessor::SlotState::loading: stateText = "LOADING"; break;
        case AmpsurdProcessor::SlotState::missing: stateText = "FILE MISSING"; break;
        case AmpsurdProcessor::SlotState::error:   stateText = "CANNOT LOAD"; break;
        case AmpsurdProcessor::SlotState::empty:
        case AmpsurdProcessor::SlotState::loaded:  break;
    }
    if (stateText.isNotEmpty())
        drawLabel(g, stateText, { 40, 10, getWidth() - 40 - 60, 18 }, textDim, juce::Justification::centred);

    // percentage = actual share of the blend (mute / solo aware)
    const bool loaded = isLoadedState(status.state);
    if (loaded)
    {
        g.setColour(audible ? text : textFaint);
        g.setFont(Fonts::get().medium(14.0f).withExtraKerningFactor(0.02f));
        g.drawText(juce::String(juce::roundToInt(effective)) + "%", juce::Rectangle<int>(getWidth() - 14 - 52, 9, 52, 20),
                   juce::Justification::centredRight, false);
    }

    // the filename - large, fixed size, wrapped, centred
    const auto area = nameArea().toFloat();
    if (status.state == AmpsurdProcessor::SlotState::empty)
    {
        drawLabel(g, "EMPTY", area.toNearestInt().withTrimmedBottom(18), textFaint, juce::Justification::centred, 11.0f);
        g.setColour(textFaint);
        g.setFont(Fonts::get().regular(12.0f));
        g.drawText("Load or drop a .nam file", area.toNearestInt().withTrimmedTop(22), juce::Justification::centred, false);
    }
    else
    {
        const bool dim = !audible || status.state != AmpsurdProcessor::SlotState::loaded;
        FilenameLayout::draw(g, status.fileName, area, dim ? textFaint : text);
    }
}

void SlotComponent::refresh(bool isSelected)
{
    const auto st = proc.getSlotStatus(slot);
    const float eff = proc.getEffectivePercent(slot);
    const bool aud = proc.isSlotAudible(slot);
    const bool changed = st.state != status.state || st.path != status.path || std::abs(eff - effective) > 0.05f
                         || aud != audible || isSelected != selected;
    status = st;
    effective = eff;
    audible = aud;
    selected = isSelected;

    const bool loaded = isLoadedState(st.state);
    editButton.setEnabled(loaded);
    soloButton.setEnabled(loaded);
    muteButton.setEnabled(loaded);
    fader.setEnabled(loaded && !proc.isFrankensteinOn()); // Frankenstein replaces the fader blend
    editButton.setToggleState(selected, juce::dontSendNotification);

    juce::String tip = st.state == AmpsurdProcessor::SlotState::empty ? juce::String("Empty slot") : st.info;
    if (st.state != AmpsurdProcessor::SlotState::empty) tip = st.path + "\n" + st.info.fromFirstOccurrenceOf("\n", false, false);
    if (tip != lastTooltip) { setTooltip(tip); lastTooltip = tip; }

    fader.repaint();
    if (changed) repaint();
}

void SlotComponent::chooseFile()
{
    auto start = juce::File(status.path).getParentDirectory();
    chooser = std::make_unique<juce::FileChooser>("Load NAM capture into slot " + juce::String(slot + 1),
                                                  start.isDirectory() ? start : juce::File(), "*.nam");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             const auto f = fc.getResult();
                             if (f.existsAsFile()) proc.loadCapture(slot, f);
                         });
}

void SlotComponent::mouseUp(const juce::MouseEvent& e)
{
    if (!e.mods.isPopupMenu()) return;
    juce::PopupMenu m;
    m.addItem(1, "Load NAM...");
    m.addItem(2, "Remove NAM", status.state != AmpsurdProcessor::SlotState::empty);
    m.addItem(3, "Show file location", juce::File(status.path).exists());
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
        if (r == 1) chooseFile();
        if (r == 2) proc.unloadCapture(slot);
        if (r == 3) juce::File(status.path).revealToUser();
    });
}

bool SlotComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && files[0].endsWithIgnoreCase(".nam");
}

void SlotComponent::filesDropped(const juce::StringArray& files, int, int)
{
    dragHover = false;
    if (!files.isEmpty()) proc.loadCapture(slot, juce::File(files[0]));
    repaint();
}

// =============================================================================================
// EqGraph
// =============================================================================================
namespace
{
constexpr float kGraphMinF = 20.0f, kGraphMaxF = 20000.0f, kGraphRangeDb = 20.0f;
}

EqGraph::EqGraph(AmpsurdProcessor& p) : proc(p) {}

juce::RangedAudioParameter* EqGraph::bandParam(int band, const char* name) const
{
    return proc.params.getParameter(AmpsurdProcessor::bandParamId(slot, band, name));
}

std::array<ampsurd::EqBand, ampsurd::ParametricEq::kNumBands> EqGraph::bands() const
{
    std::array<ampsurd::EqBand, ampsurd::ParametricEq::kNumBands> b {};
    if (slot < 0) return b;
    for (int i = 0; i < ampsurd::ParametricEq::kNumBands; ++i)
        b[(size_t) i] = { paramValue(proc, AmpsurdProcessor::bandParamId(slot, i, "freq")),
                          paramValue(proc, AmpsurdProcessor::bandParamId(slot, i, "gain")),
                          paramValue(proc, AmpsurdProcessor::bandParamId(slot, i, "q")) };
    return b;
}

juce::Rectangle<float> EqGraph::plotArea() const { return getLocalBounds().toFloat().reduced(1.0f).withTrimmedBottom(16.0f); }

float EqGraph::xForFreq(float f) const
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * std::log(f / kGraphMinF) / std::log(kGraphMaxF / kGraphMinF);
}

float EqGraph::freqForX(float x) const
{
    const auto r = plotArea();
    return kGraphMinF * std::pow(kGraphMaxF / kGraphMinF, juce::jlimit(0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
}

float EqGraph::yForGain(float gdb) const
{
    const auto r = plotArea();
    return r.getCentreY() - gdb / kGraphRangeDb * r.getHeight() * 0.5f;
}

float EqGraph::gainForY(float y) const
{
    const auto r = plotArea();
    return (r.getCentreY() - y) / (r.getHeight() * 0.5f) * kGraphRangeDb;
}

juce::Point<float> EqGraph::nodePos(int i, const std::array<ampsurd::EqBand, ampsurd::ParametricEq::kNumBands>& b) const
{
    // cut bands have no gain: their point sits on the 0 dB line at the corner frequency
    const bool bell = ampsurd::ParametricEq::bandType(i) == ampsurd::ParametricEq::BandType::bell;
    return { xForFreq(b[(size_t) i].freqHz), yForGain(bell ? b[(size_t) i].gainDb : 0.0f) };
}

int EqGraph::bandAt(juce::Point<float> p, float maxDistance) const
{
    const auto b = bands();
    int best = -1;
    float bestD = maxDistance < 0.0f ? std::numeric_limits<float>::max() : maxDistance;
    for (int i = 0; i < ampsurd::ParametricEq::kNumBands; ++i)
    {
        const float d = p.getDistanceFrom(nodePos(i, b));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void EqGraph::paint(juce::Graphics& g)
{
    const auto r = plotArea();
    g.setColour(line);
    g.drawRect(r, 1.0f);

    // grid
    g.setFont(Fonts::get().regular(11.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq(f);
        g.setColour(grid);
        g.fillRect(x - 0.5f, r.getY() + 1.0f, 1.0f, r.getHeight() - 2.0f);
        g.setColour(textDim);
        const juce::String lbl = f >= 1000.0f ? juce::String((int) (f / 1000.0f)) + "k" : juce::String((int) f);
        g.drawText(lbl, juce::Rectangle<float>(x - 20.0f, r.getBottom() + 2.0f, 40.0f, 13.0f), juce::Justification::centred, false);
    }
    for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
    {
        g.setColour(grid);
        g.fillRect(r.getX() + 1.0f, yForGain(db) - 0.5f, r.getWidth() - 2.0f, 1.0f);
        g.setColour(textDim);
        g.drawText((db > 0 ? "+" : "") + juce::String((int) db), juce::Rectangle<float>(r.getX() + 4.0f, yForGain(db) - 7.0f, 30.0f, 14.0f),
                   juce::Justification::centredLeft, false);
    }
    g.setColour(line);
    g.fillRect(r.getX() + 1.0f, yForGain(0.0f) - 0.5f, r.getWidth() - 2.0f, 1.0f);
    g.setColour(textDim);
    g.drawText("0", juce::Rectangle<float>(r.getX() + 4.0f, yForGain(0.0f) - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft, false);

    if (slot < 0) return;

    const auto b = bands();
    const bool eqOn = paramValue(proc, AmpsurdProcessor::slotParamId(slot, "eqOn")) > 0.5f;
    const double sr = juce::jmax(44100.0, proc.getSampleRateForUi());

    // Global EQ, shown behind an AMP's EQ for information only (never drawn with points, never
    // hit-tested): what will additionally happen to the complete blend later in the chain.
    if (slot != AmpsurdProcessor::kGlobalEq && proc.isGlobalEqOn())
    {
        const auto gb = proc.getEqBands(AmpsurdProcessor::kGlobalEq);
        if (!ampsurd::ParametricEq::isFlat(gb))
        {
            juce::Path bg;
            for (int i = 0; i <= (int) r.getWidth(); ++i)
            {
                const float x = r.getX() + (float) i;
                const float db = (float) ampsurd::ParametricEq::magnitudeDb(gb, freqForX(x), sr);
                const float y = juce::jlimit(r.getY(), r.getBottom(), yForGain(db));
                if (i == 0) bg.startNewSubPath(x, y); else bg.lineTo(x, y);
            }
            g.setColour(textFaint.withAlpha(0.75f));
            g.strokePath(bg, juce::PathStrokeType(1.0f));
            g.setFont(Fonts::get().regular(11.5f));
            g.drawText("grey line = GLOBAL EQ (applied after the blend, edit it with the GLOBAL EQ button)",
                       r.reduced(10.0f, 6.0f).removeFromBottom(14.0f).withTrimmedLeft(30.0f), juce::Justification::centredRight, false);
        }
    }

    // curve
    juce::Path curve;
    const int steps = (int) r.getWidth();
    for (int i = 0; i <= steps; ++i)
    {
        const float x = r.getX() + (float) i;
        const float db = (float) ampsurd::ParametricEq::magnitudeDb(b, freqForX(x), sr);
        const float y = juce::jlimit(r.getY(), r.getBottom(), yForGain(db));
        if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    g.setColour(eqOn ? text : textFaint);
    g.strokePath(curve, juce::PathStrokeType(1.5f));

    // nodes
    for (int i = 0; i < ampsurd::ParametricEq::kNumBands; ++i)
    {
        const auto pos = nodePos(i, b);
        const float x = pos.x, y = pos.y;
        const bool hot = i == hoverBand || i == activeBand;
        const bool bell = ampsurd::ParametricEq::bandType(i) == ampsurd::ParametricEq::BandType::bell;
        const bool used = bell ? b[(size_t) i].gainDb != 0.0f : ampsurd::ParametricEq::isCutActive(i, b[(size_t) i].freqHz);
        const float rad = hot ? 6.5f : 5.0f;
        g.setColour(background);
        g.fillEllipse(x - rad, y - rad, rad * 2, rad * 2);
        g.setColour(eqOn ? text : textDim);
        if (used || hot)
            g.fillEllipse(x - rad + 1.5f, y - rad + 1.5f, rad * 2 - 3.0f, rad * 2 - 3.0f);
        g.drawEllipse(x - rad, y - rad, rad * 2, rad * 2, 1.2f);
        if (hot)
        {
            g.setFont(Fonts::get().semibold(11.0f));
            g.drawText(juce::String(i + 1), juce::Rectangle<float>(x - 10.0f, y - rad - 15.0f, 20.0f, 12.0f), juce::Justification::centred, false);
        }
    }

    // readout
    const int shown = activeBand >= 0 ? activeBand : hoverBand;
    if (shown >= 0)
    {
        const auto& bb = b[(size_t) shown];
        const auto type = ampsurd::ParametricEq::bandType(shown);
        const juce::String f = bb.freqHz >= 1000.0f ? juce::String(bb.freqHz / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(bb.freqHz)) + " Hz";
        juce::String t;
        if (type == ampsurd::ParametricEq::BandType::bell)
            t = "BAND " + juce::String(shown + 1) + "     " + f + "     " + (bb.gainDb >= 0 ? "+" : "")
                + juce::String(bb.gainDb, 1) + " dB     Q " + juce::String(bb.q, 2);
        else
        {
            const juce::String name = type == ampsurd::ParametricEq::BandType::lowCut ? "LOW CUT" : "HIGH CUT";
            const float q = juce::jlimit(ampsurd::ParametricEq::kMinCutQ, ampsurd::ParametricEq::kMaxCutQ, bb.q);
            t = ampsurd::ParametricEq::isCutActive(shown, bb.freqHz)
                    ? name + "     " + f + "     12 dB/oct     Q " + juce::String(q, 2)
                    : name + "     off  (drag " + juce::String(type == ampsurd::ParametricEq::BandType::lowCut ? "right" : "left") + " to use)";
        }
        g.setColour(text);
        g.setFont(Fonts::get().medium(12.0f));
        g.drawText(t, r.reduced(10.0f, 6.0f).removeFromTop(16.0f).withTrimmedLeft(30.0f), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour(textDim);
        g.setFont(Fonts::get().regular(12.0f));
        g.drawText("Drag a point: left/right = frequency, up/down = gain.  Wheel = width (Q) of the nearest point.  Double-click = reset.",
                   r.reduced(10.0f, 6.0f).removeFromTop(16.0f).withTrimmedLeft(30.0f), juce::Justification::centredLeft, false);
    }
    if (!eqOn)
        drawLabel(g, "EQ OFF", r.reduced(10.0f, 6.0f).removeFromTop(16.0f).toNearestInt(), textDim, juce::Justification::centredRight);
}

void EqGraph::refreshIfChanged()
{
    if (slot < 0) return;
    std::array<float, 3 * ampsurd::ParametricEq::kNumBands + 1> now {};
    const auto b = bands();
    for (int i = 0; i < ampsurd::ParametricEq::kNumBands; ++i)
    {
        now[(size_t) (3 * i)] = b[(size_t) i].freqHz;
        now[(size_t) (3 * i + 1)] = b[(size_t) i].gainDb;
        now[(size_t) (3 * i + 2)] = b[(size_t) i].q;
    }
    now.back() = paramValue(proc, AmpsurdProcessor::slotParamId(slot, "eqOn"));
    bool changed = now != lastSeen;
    if (slot != AmpsurdProcessor::kGlobalEq)
    {
        // the background Global EQ curve can change too (automation, preset)
        std::array<float, 3 * ampsurd::ParametricEq::kNumBands + 1> g {};
        const auto gb = proc.getEqBands(AmpsurdProcessor::kGlobalEq);
        for (int i = 0; i < ampsurd::ParametricEq::kNumBands; ++i)
        {
            g[(size_t) (3 * i)] = gb[(size_t) i].freqHz;
            g[(size_t) (3 * i + 1)] = gb[(size_t) i].gainDb;
            g[(size_t) (3 * i + 2)] = gb[(size_t) i].q;
        }
        g.back() = proc.isGlobalEqOn() ? 1.0f : 0.0f;
        changed = changed || g != lastGlobal;
        lastGlobal = g;
    }
    if (changed) { lastSeen = now; repaint(); }
}

void EqGraph::mouseMove(const juce::MouseEvent& e)
{
    // the nearest point is highlighted: it is the one the mouse wheel changes
    const int b = slot >= 0 ? bandAt(e.position, -1.0f) : -1;
    if (b != hoverBand) { hoverBand = b; repaint(); }
    setMouseCursor(bandAt(e.position, kGrabRadius) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void EqGraph::mouseExit(const juce::MouseEvent&)
{
    if (hoverBand >= 0) { hoverBand = -1; repaint(); }
}

void EqGraph::mouseDown(const juce::MouseEvent& e)
{
    if (slot < 0) return;
    activeBand = bandAt(e.position, kGrabRadius);
    if (activeBand < 0) return;
    if (auto* f = bandParam(activeBand, "freq")) f->beginChangeGesture();
    if (ampsurd::ParametricEq::bandType(activeBand) == ampsurd::ParametricEq::BandType::bell)
        if (auto* gp = bandParam(activeBand, "gain")) gp->beginChangeGesture();
    repaint();
}

void EqGraph::mouseDrag(const juce::MouseEvent& e)
{
    if (slot < 0 || activeBand < 0) return;
    const auto r = plotArea();
    const float x = juce::jlimit(r.getX(), r.getRight(), (float) e.position.x);
    const float y = juce::jlimit(r.getY(), r.getBottom(), (float) e.position.y);
    if (auto* f = bandParam(activeBand, "freq")) f->setValueNotifyingHost(f->convertTo0to1(freqForX(x)));
    if (ampsurd::ParametricEq::bandType(activeBand) != ampsurd::ParametricEq::BandType::bell)
    {
        repaint(); // cut bands: frequency only
        return;
    }
    if (auto* gp = bandParam(activeBand, "gain"))
    {
        float gdb = juce::jlimit(-18.0f, 18.0f, gainForY(y));
        if (std::abs(gdb) < 0.25f) gdb = 0.0f; // easy return to flat
        gp->setValueNotifyingHost(gp->convertTo0to1(gdb));
    }
    repaint();
}

void EqGraph::mouseUp(const juce::MouseEvent&)
{
    if (activeBand >= 0)
    {
        if (auto* f = bandParam(activeBand, "freq")) f->endChangeGesture();
        if (ampsurd::ParametricEq::bandType(activeBand) == ampsurd::ParametricEq::BandType::bell)
            if (auto* gp = bandParam(activeBand, "gain")) gp->endChangeGesture();
    }
    activeBand = -1;
    repaint();
}

void EqGraph::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (slot < 0) return;
    const int b = bandAt(e.position, kGrabRadius);
    if (b < 0) return;
    const auto d = ampsurd::ParametricEq::defaultBands();
    for (auto [name, v] : { std::pair<const char*, float> { "freq", d[(size_t) b].freqHz }, { "gain", 0.0f }, { "q", d[(size_t) b].q } })
        if (auto* p = bandParam(b, name))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(v));
            p->endChangeGesture();
        }
    repaint();
}

void EqGraph::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (slot < 0) return;
    const int b = activeBand >= 0 ? activeBand : bandAt(e.position, -1.0f); // nearest point
    if (b < 0) return;
    if (auto* q = bandParam(b, "q"))
    {
        const bool bell = ampsurd::ParametricEq::bandType(b) == ampsurd::ParametricEq::BandType::bell;
        const float lo = bell ? ampsurd::ParametricEq::kMinQ : ampsurd::ParametricEq::kMinCutQ;
        const float hi = bell ? ampsurd::ParametricEq::kMaxQ : ampsurd::ParametricEq::kMaxCutQ;
        const float cur = juce::jlimit(lo, hi, q->convertFrom0to1(q->getValue()));
        const float nv = juce::jlimit(lo, hi, cur * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f));
        q->beginChangeGesture();
        q->setValueNotifyingHost(q->convertTo0to1(nv));
        q->endChangeGesture();
    }
    hoverBand = b;
    repaint();
}

// =============================================================================================
// AlignPanel
// =============================================================================================
AlignPanel::AlignPanel(AmpsurdProcessor& p) : proc(p)
{
    for (auto* b : { &autoButton, &freeButton, &resetButton })
        addAndMakeVisible(*b);
    autoButton.onClick = [this] { if (slot >= 0) setParamValue(proc, AmpsurdProcessor::slotParamId(slot, "align"), 0.0f); };
    freeButton.onClick = [this] { if (slot >= 0) setParamValue(proc, AmpsurdProcessor::slotParamId(slot, "align"), 1.0f); };
    resetButton.onClick = [this] { if (slot >= 0) proc.resetAlignment(slot); };
    autoButton.setTooltip("AMPSURD measures the captures and lines up their timing and polarity automatically");
    freeButton.setTooltip("Shift this amp against the others by ear, starting from the AUTO alignment");
    resetButton.setTooltip("Back to AUTO with no manual offset");

    for (auto* s : { &timeSlider, &phaseSlider })
    {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 74, 20);
        addAndMakeVisible(*s);
    }
    timeSlider.setTooltip("TIME: delays this amp by a fraction of a millisecond (affects high frequencies more)");
    phaseSlider.setTooltip("PHASE: rotates the phase of this amp equally at all frequencies, without delay");
}

void AlignPanel::setSlot(int s)
{
    slot = s;
    timeAtt.reset();
    phaseAtt.reset();
    if (slot >= 0)
    {
        timeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params, AmpsurdProcessor::slotParamId(slot, "time"), timeSlider);
        phaseAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params, AmpsurdProcessor::slotParamId(slot, "phase"), phaseSlider);
        timeSlider.textFromValueFunction = [](double v) { return (v >= 0 ? "+" : "") + juce::String(v, 3) + " ms"; };
        phaseSlider.textFromValueFunction = [](double v) { return (v >= 0 ? "+" : "") + juce::String(v, 0) + juce::String::charToString(0x00b0); };
        timeSlider.updateText();
        phaseSlider.updateText();
    }
    refresh();
}

void AlignPanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop(24);
    auto modes = r.removeFromTop(28);
    const int bw = (modes.getWidth() - 8) / 2;
    autoButton.setBounds(modes.removeFromLeft(bw));
    freeButton.setBounds(modes.removeFromRight(bw));
    r.removeFromTop(52);
    auto t = r.removeFromTop(24);
    t.removeFromLeft(52);
    timeSlider.setBounds(t);
    r.removeFromTop(8);
    auto ph = r.removeFromTop(24);
    ph.removeFromLeft(52);
    phaseSlider.setBounds(ph);
    r.removeFromTop(12);
    resetButton.setBounds(r.removeFromTop(26).removeFromLeft(bw));
}

void AlignPanel::paint(juce::Graphics& g)
{
    drawLabel(g, "ALIGNMENT", { 0, 0, getWidth(), 18 }, textDim);
    g.setColour(text);
    g.setFont(Fonts::get().regular(12.0f));
    g.drawText(line1, 0, 62, getWidth(), 18, juce::Justification::centredLeft, true);
    g.setColour(textDim);
    g.drawText(line2, 0, 80, getWidth(), 18, juce::Justification::centredLeft, true);
    const bool free = slot >= 0 && paramValue(proc, AmpsurdProcessor::slotParamId(slot, "align")) > 0.5f;
    drawLabel(g, "TIME", { 0, timeSlider.getY(), 50, timeSlider.getHeight() }, free ? text : textFaint);
    drawLabel(g, "PHASE", { 0, phaseSlider.getY(), 50, phaseSlider.getHeight() }, free ? text : textFaint);
}

void AlignPanel::refresh()
{
    const bool has = slot >= 0;
    const bool free = has && paramValue(proc, AmpsurdProcessor::slotParamId(slot, "align")) > 0.5f;
    autoButton.setToggleState(has && !free, juce::dontSendNotification);
    freeButton.setToggleState(free, juce::dontSendNotification);
    timeSlider.setEnabled(free);
    phaseSlider.setEnabled(free);
    resetButton.setEnabled(has);

    juce::String l1, l2;
    if (has)
    {
        const auto a = proc.getAlignInfo(slot);
        if (!a.measured)
            l1 = "Not measured yet";
        else if (a.isReference)
        {
            l1 = "Timing reference for the other amps";
            l2 = "Offset " + juce::String(a.autoOffsetMs, 3) + " ms";
        }
        else if (!a.reliable)
        {
            l1 = "Too different to align - left as is";
            l2 = "Low-end match " + juce::String(a.corrBefore, 2);
        }
        else
        {
            l1 = "Offset " + juce::String(a.autoOffsetMs, 3) + " ms,  polarity " + (a.polarity < 0 ? "inverted" : "normal");
            l2 = "Low-end match " + juce::String(a.corrBefore, 2) + "  ->  " + juce::String(a.corrAfter, 2);
        }
    }
    if (l1 != line1 || l2 != line2) { line1 = l1; line2 = l2; repaint(); }
}

// =============================================================================================
// EditPanel
// =============================================================================================
EditPanel::EditPanel(AmpsurdProcessor& p) : proc(p), graph(p), align(p)
{
    addAndMakeVisible(graph);
    addAndMakeVisible(align);
    for (auto* b : { &eqOnButton, &flatButton, &removeButton, &closeButton })
        addAndMakeVisible(*b);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    closeButton.setTooltip("Close EDIT and return to the gate and tuner");
    eqOnButton.setClickingTogglesState(true);
    flatButton.onClick = [this] {
        if (slot < 0) return;
        const auto d = ampsurd::ParametricEq::defaultBands();
        for (int b = 0; b < ampsurd::ParametricEq::kNumBands; ++b)
        {
            setParamValue(proc, AmpsurdProcessor::bandParamId(slot, b, "gain"), 0.0f);
            setParamValue(proc, AmpsurdProcessor::bandParamId(slot, b, "freq"), d[(size_t) b].freqHz);
            setParamValue(proc, AmpsurdProcessor::bandParamId(slot, b, "q"), d[(size_t) b].q);
        }
    };
    removeButton.onClick = [this] { if (slot >= 0) proc.unloadCapture(slot); };
    flatButton.setTooltip("Reset all ten bands of this amp's EQ");
    setSlot(-1);
}

void EditPanel::setSlot(int s)
{
    slot = s;
    graph.setSlot(s);
    align.setSlot(s);
    eqOnAtt.reset();
    if (s >= 0)
        eqOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params, AmpsurdProcessor::slotParamId(s, "eqOn"), eqOnButton);
    for (juce::Component* c : { (juce::Component*) &graph, (juce::Component*) &align, (juce::Component*) &eqOnButton,
                                (juce::Component*) &flatButton, (juce::Component*) &removeButton,
                                (juce::Component*) &closeButton })
        c->setVisible(s >= 0);
    refresh();
    repaint();
}

void EditPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(26);
    closeButton.setBounds(header.removeFromRight(72));
    header.removeFromRight(8);
    removeButton.setBounds(header.removeFromRight(100));
    header.removeFromRight(8);
    flatButton.setBounds(header.removeFromRight(64));
    header.removeFromRight(8);
    eqOnButton.setBounds(header.removeFromRight(64));
    r.removeFromTop(10);
    auto right = r.removeFromRight(300);
    r.removeFromRight(24);
    graph.setBounds(r);
    align.setBounds(right);
}

void EditPanel::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    if (slot < 0)
    {
        g.setColour(textFaint);
        g.setFont(Fonts::get().regular(13.0f));
        g.drawText("Press EDIT on an amp to adjust its EQ and alignment. The blend works without it.",
                   getLocalBounds(), juce::Justification::centred, false);
        return;
    }
    auto header = getLocalBounds().reduced(16, 12).removeFromTop(26);
    drawLabel(g, "EDIT  /  AMP " + juce::String(slot + 1), header.removeFromLeft(130), text, juce::Justification::centredLeft, 11.0f);
    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawText(title, header.withTrimmedRight(340), juce::Justification::centredLeft, true);
}

void EditPanel::refresh()
{
    if (slot >= 0)
    {
        const auto st = proc.getSlotStatus(slot);
        if (st.fileName != title) { title = st.fileName; repaint(); }
        if (st.state == AmpsurdProcessor::SlotState::empty)
        {
            setSlot(-1);
            return;
        }
        graph.refreshIfChanged();
        align.refresh();
    }
}

// =============================================================================================
// GlobalEqPanel: the same EQ as the amps, on the complete blend; no alignment.
// =============================================================================================
GlobalEqPanel::GlobalEqPanel(AmpsurdProcessor& p) : proc(p), graph(p)
{
    addAndMakeVisible(graph);
    graph.setSlot(AmpsurdProcessor::kGlobalEq);
    for (auto* b : { &eqOnButton, &flatButton, &closeButton })
        addAndMakeVisible(*b);
    eqOnButton.setClickingTogglesState(true);
    eqOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.params, AmpsurdProcessor::slotParamId(AmpsurdProcessor::kGlobalEq, "eqOn"), eqOnButton);
    eqOnButton.setTooltip("Switch the Global EQ on or off (it shapes the complete sound, after the blend)");
    flatButton.setTooltip("Reset all ten bands of the Global EQ");
    closeButton.setTooltip("Close the Global EQ and return to the gate and tuner");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    flatButton.onClick = [this] {
        const auto d = ampsurd::ParametricEq::defaultBands();
        for (int b = 0; b < ampsurd::ParametricEq::kNumBands; ++b)
        {
            setParamValue(proc, AmpsurdProcessor::bandParamId(AmpsurdProcessor::kGlobalEq, b, "gain"), 0.0f);
            setParamValue(proc, AmpsurdProcessor::bandParamId(AmpsurdProcessor::kGlobalEq, b, "freq"), d[(size_t) b].freqHz);
            setParamValue(proc, AmpsurdProcessor::bandParamId(AmpsurdProcessor::kGlobalEq, b, "q"), d[(size_t) b].q);
        }
    };
}

void GlobalEqPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(26);
    closeButton.setBounds(header.removeFromRight(72));
    header.removeFromRight(8);
    flatButton.setBounds(header.removeFromRight(64));
    header.removeFromRight(8);
    eqOnButton.setBounds(header.removeFromRight(64));
    r.removeFromTop(10);
    graph.setBounds(r);
}

void GlobalEqPanel::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    auto header = getLocalBounds().reduced(16, 12).removeFromTop(26);
    drawLabel(g, "GLOBAL EQ", header.removeFromLeft(110), text, juce::Justification::centredLeft, 11.0f);
    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawText("Shapes the complete sound: after the blend / Frankenstein, before OUTPUT and the limiter",
               header.withTrimmedRight(240), juce::Justification::centredLeft, true);
}

void GlobalEqPanel::refresh()
{
    graph.refreshIfChanged();
}

// ---------------------------------------------------------------------------------------------
GlobalEqButton::GlobalEqButton(AmpsurdProcessor& p) : proc(p)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Open the Global EQ (final tone shaping of the complete sound)");
}

void GlobalEqButton::refresh()
{
    const bool on = proc.isGlobalEqOn();
    const bool flat = ampsurd::ParametricEq::isFlat(proc.getEqBands(AmpsurdProcessor::kGlobalEq));
    if (on != shownOn || flat != shownFlat) { shownOn = on; shownFlat = flat; repaint(); }
}

void GlobalEqButton::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool hover = isMouseOver();
    // ON: lit background and strong outline, so an active Global EQ can't be overlooked.
    if (shownOn)
    {
        g.setColour(text.withAlpha(hover ? 0.20f : 0.14f));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(lineStrong);
        g.drawRoundedRectangle(r, 3.0f, 1.2f);
    }
    else
    {
        g.setColour(hover ? raised : background);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(hover ? lineStrong : line);
        g.drawRoundedRectangle(r, 3.0f, 1.0f);
    }

    // status dot at the top
    const float cx = r.getCentreX();
    g.setColour(shownOn ? text : textFaint);
    if (shownOn) g.fillEllipse(cx - 4.0f, r.getY() + 12.0f, 8.0f, 8.0f);
    else g.drawEllipse(cx - 4.0f, r.getY() + 12.0f, 8.0f, 8.0f, 1.2f);

    // vertical text, reading bottom to top
    const juce::String label = juce::String("GLOBAL EQ   ") + (shownOn ? (shownFlat ? "ON (FLAT)" : "ON") : "OFF");
    const auto area = r.withTrimmedTop(28.0f).reduced(0.0f, 8.0f);
    juce::Graphics::ScopedSaveState ss(g);
    g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, area.getCentreX(), area.getCentreY()));
    const auto rotated = juce::Rectangle<float>(area.getHeight(), area.getWidth()).withCentre(area.getCentre());
    g.setColour(shownOn ? text : textDim);
    g.setFont(Fonts::get().semibold(12.0f).withExtraKerningFactor(0.08f));
    g.drawText(label, rotated, juce::Justification::centred, false);
}

void GlobalEqButton::mouseUp(const juce::MouseEvent& e)
{
    if (getLocalBounds().contains(e.getPosition()) && onClick) onClick();
}

// =============================================================================================
// Gate
// =============================================================================================
namespace
{
constexpr float kGateMinDb = -96.0f, kGateMaxDb = 0.0f;
}

GatePanel::GatePanel(AmpsurdProcessor& p) : proc(p)
{
    onButton.setClickingTogglesState(true);
    addAndMakeVisible(onButton);
    onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params, "gateOn", onButton);
    for (auto* s : { &thresholdSlider, &decaySlider })
    {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 74, 20);
        addAndMakeVisible(*s);
    }
    thrAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, "gateThreshold", thresholdSlider);
    decAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, "gateDecay", decaySlider);
    thresholdSlider.textFromValueFunction = [](double v) { return juce::String(v, 1) + " dB"; };
    decaySlider.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; };
    thresholdSlider.setDoubleClickReturnValue(true, ampsurd::NoiseGate::kDefaultThresholdDb);
    decaySlider.setDoubleClickReturnValue(true, ampsurd::NoiseGate::kDefaultDecayMs);
    thresholdSlider.updateText();
    decaySlider.updateText();
    thresholdSlider.setTooltip("Below this guitar level the gate closes. Set it just above the noise you hear between notes");
    decaySlider.setTooltip("How quickly the sound fades out when the gate closes");
    onButton.setTooltip("Gate on/off");
}

juce::Rectangle<float> GatePanel::meterArea() const { return { 16.0f, 64.0f, (float) getWidth() - 32.0f, 22.0f }; }

float GatePanel::xForDb(float db) const
{
    const auto m = meterArea();
    return m.getX() + m.getWidth() * (juce::jlimit(kGateMinDb, kGateMaxDb, db) - kGateMinDb) / (kGateMaxDb - kGateMinDb);
}

float GatePanel::dbForX(float x) const
{
    const auto m = meterArea();
    return kGateMinDb + (kGateMaxDb - kGateMinDb) * juce::jlimit(0.0f, 1.0f, (x - m.getX()) / m.getWidth());
}

void GatePanel::resized()
{
    onButton.setBounds(getWidth() - 16 - 56, 12, 56, 24);
    auto r = getLocalBounds().reduced(16, 0);
    r.removeFromTop(116);
    auto t = r.removeFromTop(26);
    t.removeFromLeft(86);
    thresholdSlider.setBounds(t);
    r.removeFromTop(10);
    auto d = r.removeFromTop(26);
    d.removeFromLeft(86);
    decaySlider.setBounds(d);
}

void GatePanel::paint(juce::Graphics& g)
{
    drawLabel(g, "NOISE GATE", { 16, 12, 200, 24 }, text, juce::Justification::centredLeft, 11.5f);
    const bool on = paramValue(proc, "gateOn") > 0.5f;

    // input level vs threshold (drag the marker to set the threshold)
    const auto m = meterArea();
    g.setColour(line);
    g.drawRect(m, 1.0f);
    const float lvlDb = juce::Decibels::gainToDecibels(level, -120.0f);
    g.setColour(textDim);
    g.fillRect(juce::Rectangle<float>(m.getX() + 1.0f, m.getY() + 1.0f, juce::jmax(0.0f, xForDb(lvlDb) - m.getX() - 1.0f), m.getHeight() - 2.0f));
    const float thr = paramValue(proc, "gateThreshold");
    const float tx = xForDb(thr);
    g.setColour(on ? text : textFaint);
    g.fillRect(tx - 1.0f, m.getY() - 6.0f, 2.0f, m.getHeight() + 12.0f);
    juce::Path tri;
    tri.addTriangle(tx - 5.0f, m.getY() - 10.0f, tx + 5.0f, m.getY() - 10.0f, tx, m.getY() - 4.0f);
    g.fillPath(tri);
    g.setFont(Fonts::get().regular(11.0f));
    g.setColour(textDim);
    for (float db : { -96.0f, -72.0f, -48.0f, -24.0f, 0.0f })
        g.drawText(juce::String((int) db), juce::Rectangle<float>(xForDb(db) - 20.0f, m.getBottom() + 3.0f, 40.0f, 14.0f),
                   juce::Justification::centred, false);
    drawLabel(g, "GUITAR LEVEL", { 16, 40, 200, 16 }, textDim, juce::Justification::centredLeft, 10.0f);

    // state: shown as text, never only by colour
    const juce::String state = !on ? "OFF" : gain > 0.98f ? "OPEN" : gain < 0.02f ? "CLOSED" : "CLOSING";
    const auto st = juce::Rectangle<int>(getWidth() - 16 - 90, 40, 90, 16);
    drawLabel(g, state, st, on ? text : textFaint, juce::Justification::centredRight, 10.5f);

    drawLabel(g, "THRESHOLD", { 16, thresholdSlider.getY(), 84, thresholdSlider.getHeight() }, on ? text : textFaint);
    drawLabel(g, "DECAY", { 16, decaySlider.getY(), 84, decaySlider.getHeight() }, on ? text : textFaint);
    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.0f));
    g.drawText("Listens to your clean guitar, silences the noise after the amps.",
               juce::Rectangle<int>(16, decaySlider.getBottom() + 16, getWidth() - 32, 18), juce::Justification::centredLeft, true);
}

void GatePanel::mouseDown(const juce::MouseEvent& e)
{
    const auto m = meterArea().expanded(0.0f, 12.0f);
    if (!m.contains(e.position)) return;
    draggingThreshold = true;
    if (auto* p = proc.params.getParameter("gateThreshold"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(-96.0f, -20.0f, dbForX(e.position.x))));
    }
}

void GatePanel::mouseDrag(const juce::MouseEvent& e)
{
    if (!draggingThreshold) return;
    if (auto* p = proc.params.getParameter("gateThreshold"))
        p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(-96.0f, -20.0f, dbForX(e.position.x))));
    repaint();
}

void GatePanel::mouseUp(const juce::MouseEvent&)
{
    if (!draggingThreshold) return;
    draggingThreshold = false;
    if (auto* p = proc.params.getParameter("gateThreshold")) p->endChangeGesture();
}

void GatePanel::refresh()
{
    const float pk = proc.getAndResetGateKeyPeak();
    level = pk > level ? pk : level * 0.8f;
    gain = proc.getGateGain();
    repaint();
}

// =============================================================================================
// Tuner
// =============================================================================================
TunerPanel::TunerPanel(AmpsurdProcessor& p) : proc(p)
{
    muteButton.setClickingTogglesState(true);
    addAndMakeVisible(muteButton);
    muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params, "tunerMute", muteButton);
    muteButton.setTooltip("Silence AMPSURD's output while the tuner is shown");
}

void TunerPanel::resized()
{
    muteButton.setBounds(getWidth() - 16 - 116, 12, 116, 24);
}

void TunerPanel::refresh()
{
    const auto r = proc.analyseTuner();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (r.valid)
    {
        if (r.midiNote != lastNote) { recentCount = 0; lastNote = r.midiNote; }
        recentCents[(size_t) (recentCount++ % (int) recentCents.size())] = r.cents;
        // median of the last readings: steady needle without lag
        const int n = juce::jmin(recentCount, (int) recentCents.size());
        std::array<double, 5> tmp = recentCents;
        std::sort(tmp.begin(), tmp.begin() + n);
        shown = r;
        shown.cents = tmp[(size_t) (n / 2)];
        lastValidMs = now;
    }
    else if (now - lastValidMs > 600.0)
    {
        shown.valid = false;
        lastNote = -1;
        recentCount = 0;
    }
    repaint();
}

void TunerPanel::paint(juce::Graphics& g)
{
    drawLabel(g, "TUNER", { 16, 12, 200, 24 }, text, juce::Justification::centredLeft, 11.5f);
    const auto area = getLocalBounds().reduced(16, 0).withTrimmedTop(44);

    // note name
    const auto noteArea = area.withHeight(88);
    if (!shown.valid)
    {
        g.setColour(textFaint);
        g.setFont(Fonts::get().semibold(64.0f));
        g.drawText("-", noteArea, juce::Justification::centred, false);
    }
    else
    {
        const juce::String name(shown.noteName());
        g.setColour(text);
        g.setFont(Fonts::get().semibold(64.0f));
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), name);
        g.drawText(name, noteArea, juce::Justification::centred, false);
        g.setFont(Fonts::get().medium(18.0f));
        g.setColour(textDim);
        g.drawText(juce::String(shown.octave()), juce::Rectangle<int>(noteArea.getCentreX() + w / 2 + 4, noteArea.getY() + 52, 30, 24),
                   juce::Justification::centredLeft, false);
    }

    // cents scale -50..+50
    const auto scale = juce::Rectangle<float>((float) area.getX() + 20.0f, (float) noteArea.getBottom() + 18.0f,
                                              (float) area.getWidth() - 40.0f, 26.0f);
    auto xFor = [&](double c) { return scale.getX() + scale.getWidth() * (float) ((juce::jlimit(-50.0, 50.0, c) + 50.0) / 100.0); };
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = xFor(c);
        const float h = c == 0 ? scale.getHeight() : 10.0f;
        g.setColour(c == 0 ? textDim : line);
        g.fillRect(x - 0.5f, scale.getCentreY() - h / 2.0f, 1.0f, h);
    }
    g.setFont(Fonts::get().medium(14.0f));
    g.setColour(textDim);
    g.drawText("b", juce::Rectangle<float>(scale.getX() - 20.0f, scale.getY(), 16.0f, scale.getHeight()),
               juce::Justification::centred, false);
    g.drawText("#", juce::Rectangle<float>(scale.getRight() + 4.0f, scale.getY(), 16.0f, scale.getHeight()),
               juce::Justification::centred, false);

    if (shown.valid)
    {
        const bool inTune = std::abs(shown.cents) <= 2.0;
        const float x = xFor(shown.cents);
        g.setColour(text);
        if (inTune)
            g.fillRoundedRectangle(x - 9.0f, scale.getY() - 2.0f, 18.0f, scale.getHeight() + 4.0f, 2.0f);
        else
            g.fillRoundedRectangle(x - 2.0f, scale.getY() - 2.0f, 4.0f, scale.getHeight() + 4.0f, 1.5f);

        const auto info = juce::Rectangle<int>(area.getX(), (int) scale.getBottom() + 12, area.getWidth(), 18);
        g.setFont(Fonts::get().medium(13.0f));
        g.setColour(inTune ? text : textDim);
        const juce::String cents = inTune ? juce::String("IN TUNE")
                                          : (shown.cents > 0 ? "+" : "") + juce::String(shown.cents, 1) + " cents";
        g.drawText(cents + "     " + juce::String(shown.frequencyHz, 1) + " Hz", info, juce::Justification::centred, false);
    }
    else
    {
        g.setColour(textDim);
        g.setFont(Fonts::get().regular(12.5f));
        g.drawText("Play a single open string", juce::Rectangle<int>(area.getX(), (int) scale.getBottom() + 12, area.getWidth(), 18),
                   juce::Justification::centred, false);
    }
}

// =============================================================================================
// Create Frankenstein
// =============================================================================================
namespace
{
constexpr double kMapLo = 20.0, kMapHi = 20000.0;
}

FrankensteinPanel::FrankensteinPanel(AmpsurdProcessor& p) : proc(p)
{
    for (int i = 0; i < 4; ++i)
    {
        auto& b = sectionButtons[(size_t) i];
        addAndMakeVisible(b);
        b.onClick = [this, i] { proc.setFrankensteinSections(i + 2); };
        b.setTooltip("Number of sections (amps) across the spectrum");
    }
    addAndMakeVisible(exitButton);
    exitButton.onClick = [this] { proc.setFrankenstein(false); };
    exitButton.setTooltip("Back to the normal blend (faders)");
    widthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    widthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
    addAndMakeVisible(widthSlider);
    widthAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, "frankWidth", widthSlider);
    widthSlider.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " %"; };
    widthSlider.updateText();
    widthSlider.setTooltip("How gradually one amp hands over to the next: 0 % = abrupt, 90 % = smooth morph");
}

juce::Rectangle<float> FrankensteinPanel::mapArea() const
{
    return getLocalBounds().toFloat().reduced(16.0f, 0.0f).withTrimmedTop(52.0f).withTrimmedBottom(30.0f);
}

float FrankensteinPanel::xForHz(double hz) const
{
    const auto m = mapArea();
    return m.getX() + m.getWidth() * (float) (std::log(hz / kMapLo) / std::log(kMapHi / kMapLo));
}

double FrankensteinPanel::hzForX(float x) const
{
    const auto m = mapArea();
    return kMapLo * std::pow(kMapHi / kMapLo, (double) juce::jlimit(0.0f, 1.0f, (x - m.getX()) / m.getWidth()));
}

void FrankensteinPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 12).removeFromTop(26);
    exitButton.setBounds(r.removeFromRight(64));
    r.removeFromRight(24);
    widthSlider.setBounds(r.removeFromRight(220));
    r.removeFromRight(60);
    for (int i = 3; i >= 0; --i)
    {
        sectionButtons[(size_t) i].setBounds(r.removeFromRight(30));
        r.removeFromRight(4);
    }
}

int FrankensteinPanel::dividerAt(juce::Point<float> p) const
{
    const auto m = mapArea();
    if (p.y < m.getY() - 4 || p.y > m.getBottom() + 4) return -1;
    int best = -1;
    float bestD = 8.0f;
    for (int j = 0; j < layout.numVisible - 1; ++j)
    {
        const float d = std::abs(p.x - xForHz(layout.visibleDividersHz[(size_t) j]));
        if (d < bestD) { bestD = d; best = j; }
    }
    return best;
}

void FrankensteinPanel::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    drawLabel(g, "FRANKENSTEIN", { 16, 12, 200, 26 }, text, juce::Justification::centredLeft, 11.5f);
    drawLabel(g, "SECTIONS", { sectionButtons[0].getX() - 76, 12, 70, 26 }, textDim, juce::Justification::centredRight);
    drawLabel(g, "WIDTH", { widthSlider.getX() - 56, 12, 50, 26 }, textDim, juce::Justification::centredRight);

    const auto m = mapArea();
    g.setColour(line);
    g.drawRect(m, 1.0f);

    // overlap zones (hatched), where two amps blend
    for (int j = 0; j < layout.numVisible - 1; ++j)
    {
        const float x0 = xForHz(layout.crossoverHz[(size_t) (2 * j)]), x1 = xForHz(layout.crossoverHz[(size_t) (2 * j + 1)]);
        if (x1 - x0 < 2.0f) continue;
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(juce::Rectangle<float>(x0, m.getY() + 1, x1 - x0, m.getHeight() - 2).toNearestInt());
        g.setColour(grid.brighter(0.15f));
        for (float x = x0 - m.getHeight(); x < x1; x += 7.0f)
            g.drawLine(x, m.getBottom(), x + m.getHeight(), m.getY(), 1.0f);
    }

    // sections
    for (int i = 0; i < layout.numVisible; ++i)
    {
        const auto& sec = layout.visibleSections[(size_t) i];
        const float x0 = xForHz(sec.loHz), x1 = xForHz(sec.hiHz);
        auto col = juce::Rectangle<float>(x0, m.getY(), x1 - x0, m.getHeight()).reduced(8.0f, 10.0f);
        const auto st = proc.getSlotStatus(sec.slot);
        g.setColour(text);
        g.setFont(Fonts::get().semibold(28.0f));
        g.drawText(juce::String(sec.slot + 1), col.removeFromTop(40.0f), juce::Justification::centred, false);
        g.setFont(Fonts::get().medium(13.0f));
        g.setColour(textDim);
        g.drawFittedText(st.fileName, col.removeFromTop(54.0f).toNearestInt(), juce::Justification::centredTop, 3, 1.0f);
    }

    // dividers
    for (int j = 0; j < layout.numVisible - 1; ++j)
    {
        const float x = xForHz(layout.visibleDividersHz[(size_t) j]);
        const bool merged = layout.dividerIsMerged[(size_t) j];
        const bool hot = j == hoverDivider || j == dragDivider;
        g.setColour(merged ? textFaint : text);
        if (merged)
        {
            for (float y = m.getY(); y < m.getBottom(); y += 8.0f)
                g.fillRect(x - 0.5f, y, 1.0f, 4.0f);
        }
        else
        {
            g.fillRect(x - (hot ? 1.5f : 1.0f), m.getY(), hot ? 3.0f : 2.0f, m.getHeight());
            g.fillRoundedRectangle(x - 5.0f, m.getY() - 6.0f, 10.0f, 12.0f, 2.0f);
        }
        const double hz = layout.visibleDividersHz[(size_t) j];
        const juce::String lbl = hz >= 1000.0 ? juce::String(hz / 1000.0, 2) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz";
        g.setFont(Fonts::get().medium(11.5f));
        g.drawText(lbl, juce::Rectangle<float>(x - 40.0f, m.getBottom() - 18.0f, 80.0f, 14.0f), juce::Justification::centred, false);
    }

    // frequency axis
    g.setFont(Fonts::get().regular(11.0f));
    g.setColour(textDim);
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xForHz(f);
        g.fillRect(x - 0.5f, m.getBottom(), 1.0f, 4.0f);
        g.drawText(f >= 1000 ? juce::String((int) (f / 1000)) + "k" : juce::String((int) f),
                   juce::Rectangle<float>(x - 20.0f, m.getBottom() + 5.0f, 40.0f, 13.0f), juce::Justification::centred, false);
    }

    const int hidden = juce::jlimit(2, 5, (int) std::lround(proc.params.getRawParameterValue("frankSections")->load())) - layout.numVisible;
    g.setFont(Fonts::get().regular(11.5f));
    g.setColour(textDim);
    const juce::String hint = layout.numVisible == 0 ? juce::String("All amps used here are muted or empty")
                            : hidden > 0 ? juce::String(hidden) + " section(s) hidden (muted or empty amp)"
                                         : juce::String("Drag a divider to move the hand-over. Click a section to choose its amp.");
    g.drawText(hint, juce::Rectangle<float>(130.0f, 12.0f, (float) sectionButtons[0].getX() - 76.0f - 140.0f, 26.0f),
               juce::Justification::centredLeft, true);
}

void FrankensteinPanel::refresh()
{
    layout = proc.getFrankensteinLayout();
    const int K = juce::jlimit(2, 5, (int) std::lround(proc.params.getRawParameterValue("frankSections")->load()));
    for (int i = 0; i < 4; ++i)
        sectionButtons[(size_t) i].setToggleState(i + 2 == K, juce::dontSendNotification);
    repaint();
}

void FrankensteinPanel::mouseMove(const juce::MouseEvent& e)
{
    const int d = dividerAt(e.position);
    hoverDivider = (d >= 0 && !layout.dividerIsMerged[(size_t) d]) ? d : -1;
    setMouseCursor(hoverDivider >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                   : mapArea().contains(e.position) ? juce::MouseCursor::PointingHandCursor
                                                    : juce::MouseCursor::NormalCursor);
}

void FrankensteinPanel::mouseDown(const juce::MouseEvent& e)
{
    const int d = dividerAt(e.position);
    if (d >= 0 && !layout.dividerIsMerged[(size_t) d])
    {
        dragDivider = d;
        dragParam = layout.visibleSections[(size_t) d + 1].sourceIndex - 1; // boundary below section k = divider k-1
        if (auto* p = proc.params.getParameter("frankDiv" + juce::String(dragParam + 1))) p->beginChangeGesture();
        return;
    }
    if (!mapArea().contains(e.position)) return;
    for (int i = 0; i < layout.numVisible; ++i)
        if (e.position.x >= xForHz(layout.visibleSections[(size_t) i].loHz) && e.position.x < xForHz(layout.visibleSections[(size_t) i].hiHz))
            chooseAmpForSection(i);
}

void FrankensteinPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (dragDivider < 0 || dragParam < 0) return;
    // keep dividers in order: stay at least 1/6 octave away from the neighbouring dividers
    const int K = juce::jlimit(2, 5, (int) std::lround(proc.params.getRawParameterValue("frankSections")->load()));
    double lo = 30.0, hi = 16000.0;
    if (dragParam > 0) lo = proc.params.getRawParameterValue("frankDiv" + juce::String(dragParam))->load() * std::pow(2.0, 1.0 / 6.0);
    if (dragParam < K - 2) hi = proc.params.getRawParameterValue("frankDiv" + juce::String(dragParam + 2))->load() / std::pow(2.0, 1.0 / 6.0);
    const double hz = juce::jlimit(lo, juce::jmax(lo, hi), hzForX(e.position.x));
    if (auto* p = proc.params.getParameter("frankDiv" + juce::String(dragParam + 1)))
        p->setValueNotifyingHost(p->convertTo0to1((float) hz));
    refresh();
}

void FrankensteinPanel::mouseUp(const juce::MouseEvent&)
{
    if (dragParam >= 0)
        if (auto* p = proc.params.getParameter("frankDiv" + juce::String(dragParam + 1))) p->endChangeGesture();
    dragDivider = dragParam = -1;
}

void FrankensteinPanel::chooseAmpForSection(int visibleIndex)
{
    const int k = layout.visibleSections[(size_t) visibleIndex].sourceIndex;
    juce::PopupMenu menu;
    menu.addSectionHeader("Section " + juce::String(visibleIndex + 1) + " plays:");
    for (int s = 0; s < AmpsurdProcessor::kNumSlots; ++s)
    {
        const auto st = proc.getSlotStatus(s);
        const bool usable = st.state == AmpsurdProcessor::SlotState::loaded;
        menu.addItem(s + 1, juce::String(s + 1) + "   " + (usable ? st.fileName : juce::String("(empty)")), usable,
                     s == layout.visibleSections[(size_t) visibleIndex].slot);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, k](int r) {
        if (r > 0)
            if (auto* p = proc.params.getParameter("frankAmp" + juce::String(k + 1)))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->convertTo0to1((float) (r - 1)));
                p->endChangeGesture();
            }
    });
}

// =============================================================================================
CentrePanel::CentrePanel(AmpsurdProcessor& p) : gatePanel(p), tunerPanel(p)
{
    addAndMakeVisible(gatePanel);
    addAndMakeVisible(tunerPanel);
}

void CentrePanel::resized()
{
    auto r = getLocalBounds().reduced(1);
    gatePanel.setBounds(r.removeFromLeft(r.getWidth() / 2));
    tunerPanel.setBounds(r);
}

void CentrePanel::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    g.fillRect((float) getWidth() / 2.0f, 12.0f, 1.0f, (float) getHeight() - 24.0f);
}

void CentrePanel::refresh()
{
    gatePanel.refresh();
    tunerPanel.refresh();
}

// =============================================================================================
// Meters, master
// =============================================================================================
void LevelMeter::setPeak(float linear)
{
    const float v = linear > level ? linear : level * 0.82f;
    if (v > hold) { hold = v; holdCount = 30; }
    else if (--holdCount <= 0) hold *= 0.9f;
    if (std::abs(v - level) > 1e-4f || holdCount == 29) repaint();
    level = v;
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(line);
    g.drawRect(r, 1.0f);
    auto toX = [&](float lin) {
        const float db = juce::Decibels::gainToDecibels(lin, -60.0f);
        return r.getX() + r.getWidth() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };
    g.setColour(textDim);
    g.fillRect(juce::Rectangle<float>(r.getX(), r.getY() + 1.0f, toX(level) - r.getX(), r.getHeight() - 2.0f));
    g.setColour(text);
    g.fillRect(toX(hold) - 1.0f, r.getY(), 2.0f, r.getHeight());
}

MasterPanel::MasterPanel(AmpsurdProcessor& p) : proc(p)
{
    for (auto* s : { &inputSlider, &outputSlider })
    {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
        addAndMakeVisible(*s);
    }
    inAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, "input", inputSlider);
    outAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, "output", outputSlider);
    for (auto* s : { &inputSlider, &outputSlider })
    {
        s->textFromValueFunction = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " dB"; };
        s->updateText();
        s->setDoubleClickReturnValue(true, 0.0);
    }
    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    bypassButton.setClickingTogglesState(true);
    addAndMakeVisible(bypassButton);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params, "bypass", bypassButton);
    gateButton.setClickingTogglesState(true);
    addAndMakeVisible(gateButton);
    gateAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params, "gateOn", gateButton);
    gateButton.setTooltip("Noise gate on/off (settings in the centre area when no amp is in EDIT)");
    addAndMakeVisible(frankButton);
    frankButton.onClick = [this] { proc.setFrankenstein(!proc.isFrankensteinOn()); };
    frankButton.setTooltip("Split the spectrum between the amps: e.g. lows from one amp, mids from another, highs from a third");
    inputSlider.setTooltip("Input level into all five amps (how hard they are driven)");
    outputSlider.setTooltip("Output level. AMPSURD never outputs above -1 dBFS");
}

void MasterPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 0);
    auto block = [&](juce::Slider& s, LevelMeter& m) {
        auto b = r.removeFromLeft(320);
        b.removeFromLeft(64);
        auto sl = b.removeFromLeft(156);
        s.setBounds(sl.withSizeKeepingCentre(sl.getWidth(), 24));
        b.removeFromLeft(14);
        m.setBounds(b.withSizeKeepingCentre(b.getWidth(), 6));
        r.removeFromLeft(24);
    };
    block(inputSlider, inMeter);
    block(outputSlider, outMeter);
    r.removeFromLeft(78); // room for the LIMIT indicator next to the output meter
    frankButton.setBounds(r.removeFromLeft(164).withSizeKeepingCentre(164, 28));
    r.removeFromLeft(8);
    gateButton.setBounds(r.removeFromLeft(72).withSizeKeepingCentre(72, 28));
    r.removeFromLeft(8);
    bypassButton.setBounds(r.removeFromLeft(86).withSizeKeepingCentre(86, 28));
}

void MasterPanel::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    drawLabel(g, "INPUT", { 16, 0, 60, getHeight() }, textDim);
    drawLabel(g, "OUTPUT", { 16 + 344, 0, 60, getHeight() }, textDim);

    // safety limiter: shown as text, never only by colour
    const auto lim = juce::Rectangle<int>(outMeter.getRight() + 8, getHeight() / 2 - 8, 66, 16);
    if (limitHold > 0)
    {
        g.setColour(onFill);
        g.fillRoundedRectangle(lim.toFloat(), 2.0f);
        drawLabel(g, "LIMIT " + juce::String(-limitDb, 1), lim, onText, juce::Justification::centred, 10.5f);
    }
    else
        drawLabel(g, "LIMIT", lim, textFaint, juce::Justification::centred, 10.5f);

}

void MasterPanel::refresh()
{
    inMeter.setPeak(proc.getAndResetInputPeak());
    outMeter.setPeak(proc.getAndResetOutputPeak());
    const float gr = proc.getAndResetLimiterReductionDb();
    const int oldHold = limitHold;
    if (gr > 0.05f) { limitDb = juce::jmax(gr, limitHold > 0 ? limitDb : 0.0f); limitHold = 30; }
    else if (limitHold > 0) --limitHold;
    frankButton.setToggleState(proc.isFrankensteinOn(), juce::dontSendNotification);
    if (limitHold != oldHold || limitHold > 0) repaint();
}

// =============================================================================================
// Header
// =============================================================================================
HeaderBar::HeaderBar(AmpsurdProcessor& p) : proc(p)
{
    for (auto* b : { &presetButton, &saveButton, &saveAsButton, &settingsButton })
        addAndMakeVisible(*b);
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick = [this] { save(false); };
    saveAsButton.onClick = [this] { save(true); };
    settingsButton.onClick = [this] { showSettings(); };
    presetButton.setTooltip("Presets store the complete rig: captures, mix, EQ, alignment and master");
    settingsButton.setTooltip("Settings");
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced(24, 12);
    settingsButton.setBounds(r.removeFromRight(32));
    r.removeFromRight(10);
    saveAsButton.setBounds(r.removeFromRight(80));
    r.removeFromRight(8);
    saveButton.setBounds(r.removeFromRight(64));
    r.removeFromRight(12);
    presetButton.setBounds(r.removeFromRight(260));
}

void HeaderBar::paint(juce::Graphics& g)
{
    g.setColour(text);
    g.setFont(Fonts::get().semibold(24.0f).withExtraKerningFactor(0.16f));
    g.drawText("AMPSURD", getLocalBounds().reduced(24, 0), juce::Justification::centredLeft, false);
    drawLabel(g, "PRESET", { presetButton.getX() - 64, 0, 56, getHeight() }, textDim, juce::Justification::centredRight);

    // preset name inside its button (left aligned, with a small caret)
    auto pr = presetButton.getBounds().reduced(12, 0);
    g.setColour(text);
    g.setFont(Fonts::get().medium(13.0f));
    g.drawText(presetName, pr.withTrimmedRight(18), juce::Justification::centredLeft, true);
    juce::Path caret;
    const float cx = (float) pr.getRight() - 5.0f, cy = (float) pr.getCentreY();
    caret.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.fillPath(caret);

    // settings icon: three thin lines with sliders
    auto sb = settingsButton.getBounds().toFloat().reduced(9.0f, 10.0f);
    g.setColour(text);
    for (int i = 0; i < 3; ++i)
    {
        const float y = sb.getY() + (float) i * sb.getHeight() / 2.0f;
        g.fillRect(sb.getX(), y - 0.5f, sb.getWidth(), 1.0f);
        const float kx = sb.getX() + sb.getWidth() * (i == 1 ? 0.7f : 0.3f);
        g.fillRect(kx - 1.5f, y - 3.0f, 3.0f, 6.0f);
    }
}

void HeaderBar::refresh()
{
    const auto n = proc.getCurrentPresetName();
    if (n != presetName) { presetName = n; repaint(); }
}

void HeaderBar::showPresetMenu()
{
    juce::PopupMenu m;
    m.addItem(1, "Init (empty rig)");
    const auto files = proc.listPresets();
    if (!files.isEmpty()) m.addSeparator();
    for (int i = 0; i < files.size(); ++i)
        m.addItem(100 + i, files[i].getFileNameWithoutExtension(), true, files[i] == proc.getCurrentPresetFile());
    m.addSeparator();
    m.addItem(2, "Open preset folder");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetButton).withMinimumWidth(presetButton.getWidth()),
                    [this, files](int r) {
                        if (r == 1) proc.loadInitPreset();
                        else if (r == 2) { proc.getPresetFolder().createDirectory(); proc.getPresetFolder().revealToUser(); }
                        else if (r >= 100 && r - 100 < files.size()) proc.loadPreset(files[r - 100]);
                        refresh();
                    });
}

void HeaderBar::save(bool saveAs)
{
    const auto current = proc.getCurrentPresetFile();
    if (!saveAs && current.existsAsFile())
    {
        proc.savePreset(current);
        refresh();
        return;
    }
    proc.getPresetFolder().createDirectory();
    const auto suggestion = proc.getPresetFolder().getChildFile((proc.getCurrentPresetName() == "Init" ? juce::String("My rig")
                                                                                                       : proc.getCurrentPresetName()) + ".ampsurd");
    chooser = std::make_unique<juce::FileChooser>("Save AMPSURD preset", suggestion, "*.ampsurd");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc) {
                             const auto f = fc.getResult();
                             if (f != juce::File()) proc.savePreset(f);
                             refresh();
                         });
}

void HeaderBar::showSettings()
{
    juce::PopupMenu m;
    const bool lm = proc.params.getRawParameterValue("levelMatch")->load() > 0.5f;
    m.addItem(1, "Level match (recommended)", true, lm);
    m.addSeparator();
    m.addItem(2, "Open preset folder");
    m.addItem(3, "About AMPSURD");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settingsButton), [this, lm](int r) {
        if (r == 1) setParamValue(proc, "levelMatch", lm ? 0.0f : 1.0f);
        if (r == 2) { proc.getPresetFolder().createDirectory(); proc.getPresetFolder().revealToUser(); }
        if (r == 3)
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::NoIcon, "AMPSURD " JucePlugin_VersionString,
                "Five NAM captures, blended into one guitar sound.\n\n"
                "Free software under the GNU AGPLv3.\n"
                "Uses NeuralAmpModelerCore (MIT), JUCE (AGPLv3), the VST3 SDK (MIT), Eigen (MPL-2.0) "
                "and the Inter typeface (SIL OFL 1.1).\n\n"
                "Level match: every capture is measured by AMPSURD and played at the same perceived loudness.\n"
                "Mix: percentages are kept at a constant overall loudness.");
    });
}

// =============================================================================================
// Footer
// =============================================================================================
BrandingFooter::BrandingFooter()
{
    for (int i = 0; i < 3; ++i)
        for (const char* ext : { ".svg", ".png" })
        {
            const juce::String name = "logo_" + juce::String(i + 1) + ext;
            for (int k = 0; k < BinaryData::namedResourceListSize && logos[(size_t) i] == nullptr; ++k)
                if (name == BinaryData::originalFilenames[k])
                {
                    int size = 0;
                    if (const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[k], size))
                        logos[(size_t) i] = juce::Drawable::createFromImageData(data, (size_t) size);
                }
        }
}

void BrandingFooter::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.fillRect(0.0f, 0.0f, (float) getWidth(), 1.0f);

    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.0f));
    g.drawText(cpuText, getLocalBounds().reduced(24, 0), juce::Justification::centredRight, false);

    static const char* names[3] = { "THE BLACK DEATH ENSEMBLE", "DARK MATTER", "SYMBIORBIS" };
    const int boxW = 168, boxH = 26, gap = 12, labelW = 140;
    const int total = labelW + 3 * boxW + 2 * gap;
    int x = (getWidth() - total) / 2;
    const int y = (getHeight() - boxH) / 2 + 1;
    drawLabel(g, "BROUGHT TO YOU BY", { x, y, labelW - 12, boxH }, textDim, juce::Justification::centredRight, 10.5f);
    x += labelW;
    for (int i = 0; i < 3; ++i)
    {
        const juce::Rectangle<int> box(x, y, boxW, boxH);
        if (logos[(size_t) i] != nullptr)
            logos[(size_t) i]->drawWithin(g, box.toFloat().reduced(2.0f), juce::RectanglePlacement::centred, 0.85f);
        else
        {
            g.setColour(line);
            g.drawRect(box, 1);
            drawLabel(g, names[i], box, textDim, juce::Justification::centred, 10.0f);
        }
        x += boxW + gap;
    }
}

} // namespace ampsurd::ui
