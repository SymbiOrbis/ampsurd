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
    fader.setEnabled(loaded);
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
    if (now != lastSeen) { lastSeen = now; repaint(); }
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
    for (auto* b : { &eqOnButton, &flatButton, &removeButton })
        addAndMakeVisible(*b);
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
                                (juce::Component*) &flatButton, (juce::Component*) &removeButton })
        c->setVisible(s >= 0);
    refresh();
    repaint();
}

void EditPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(26);
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
    g.drawText(title, header.withTrimmedRight(260), juce::Justification::centredLeft, true);
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
    inputSlider.setTooltip("Input level into all five amps (how hard they are driven)");
    outputSlider.setTooltip("Output level. AMPSURD never outputs above -1 dBFS");
}

void MasterPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 0);
    auto block = [&](juce::Slider& s, LevelMeter& m) {
        auto b = r.removeFromLeft(400);
        b.removeFromLeft(64);
        auto sl = b.removeFromLeft(220);
        s.setBounds(sl.withSizeKeepingCentre(sl.getWidth(), 24));
        b.removeFromLeft(14);
        m.setBounds(b.withSizeKeepingCentre(b.getWidth(), 6));
        r.removeFromLeft(24);
    };
    block(inputSlider, inMeter);
    block(outputSlider, outMeter);
    r.removeFromLeft(80); // room for the LIMIT indicator next to the output meter
    bypassButton.setBounds(r.removeFromLeft(86).withSizeKeepingCentre(86, 28));
}

void MasterPanel::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    drawLabel(g, "INPUT", { 16, 0, 60, getHeight() }, textDim);
    drawLabel(g, "OUTPUT", { 16 + 424, 0, 60, getHeight() }, textDim);

    // safety limiter: shown as text, never only by colour
    const auto lim = juce::Rectangle<int>(outMeter.getRight() + 12, getHeight() / 2 - 8, 70, 16);
    if (limitHold > 0)
    {
        g.setColour(onFill);
        g.fillRoundedRectangle(lim.toFloat(), 2.0f);
        drawLabel(g, "LIMIT " + juce::String(-limitDb, 1), lim, onText, juce::Justification::centred, 10.5f);
    }
    else
        drawLabel(g, "LIMIT", lim, textFaint, juce::Justification::centred, 10.5f);

    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawText(cpuText, getLocalBounds().reduced(16, 0), juce::Justification::centredRight, false);
}

void MasterPanel::refresh()
{
    inMeter.setPeak(proc.getAndResetInputPeak());
    outMeter.setPeak(proc.getAndResetOutputPeak());
    const float gr = proc.getAndResetLimiterReductionDb();
    const int oldHold = limitHold;
    if (gr > 0.05f) { limitDb = juce::jmax(gr, limitHold > 0 ? limitDb : 0.0f); limitHold = 30; }
    else if (limitHold > 0) --limitHold;
    const juce::String cpu = "CPU " + juce::String(juce::roundToInt(proc.getCpuLoadPercent())) + "%";
    if (cpu != cpuText || limitHold != oldHold || limitHold > 0) { cpuText = cpu; repaint(); }
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
