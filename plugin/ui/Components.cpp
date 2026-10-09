#include "Components.h"

#include "BinaryData.h"

#include <cstring>
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
    const auto font = Fonts::get().semibold(kFontSize);
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
    for (auto* b : { &loadButton, &editButton, &irButton, &soloButton, &muteButton, &removeButton })
        addAndMakeVisible(*b);
    irButton.onClick = [this] { if (onIrClicked) onIrClicked(slot); };
    irButton.setTooltip("Cabinet IR for this amp: load, bypass, IR EQ");
    removeButton.setTooltip("Remove the capture (and its IR) from this slot - click twice");
    removeButton.onClick = [this] {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (removeArmedAt > 0.0 && now - removeArmedAt < 3000.0)
        {
            removeArmedAt = -1.0;
            removeButton.setButtonText("REMOVE");
            proc.unloadCapture(slot);
        }
        else
        {
            removeArmedAt = now;
            removeButton.setButtonText("SURE?");
        }
    };
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

    panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider.setDoubleClickReturnValue(true, 0.0);
    panSlider.setTooltip("PAN: place this amp in the stereo field (double-click = centre)");
    panSlider.setMouseDragSensitivity(220);
    addAndMakeVisible(panSlider);
    panAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, AmpsurdProcessor::slotParamId(s, "pan"), panSlider);
    panSlider.onValueChange = [this] { repaint(panArea()); };
}

juce::Rectangle<int> SlotComponent::panArea() const
{
    // one row under the filename, left of the fader
    return { 14, getHeight() - 14 - 28 - 6 - 28 - 10 - 22, getWidth() - 14 - 14 - 44, 22 };
}

SlotComponent::~SlotComponent() = default;

// Slot layout (300 px high): number + % / filename (+ IR line) and fader / PAN / two button rows.
juce::Rectangle<int> SlotComponent::nameArea() const
{
    return { 14, 40, getWidth() - 14 - 14 - 44, 128 };
}

juce::Rectangle<int> SlotComponent::irLineArea() const
{
    return { 14, 170, getWidth() - 14 - 14 - 44, 16 };
}

void SlotComponent::resized()
{
    auto r = getLocalBounds().reduced(14);
    auto buttons = r.removeFromBottom(62);
    auto row1 = buttons.removeFromTop(28), row2 = buttons.removeFromBottom(28);
    const int gap = 6, bw = (row1.getWidth() - 2 * gap) / 3;
    auto place = [&](juce::Rectangle<int>& row, juce::Button& b, bool last) {
        b.setBounds(last ? row : row.removeFromLeft(bw));
        if (!last) row.removeFromLeft(gap);
    };
    place(row1, loadButton, false); place(row1, editButton, false); place(row1, irButton, true);
    place(row2, soloButton, false); place(row2, muteButton, false); place(row2, removeButton, true);
    r.removeFromBottom(10);
    fader.setBounds(r.removeFromRight(40).withTrimmedTop(22));
    auto pa = panArea();
    pa.removeFromLeft(34);  // "PAN" label
    pa.removeFromRight(34); // value
    panSlider.setBounds(pa);
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
    g.setColour(isLoadedState(status.state) ? text : textDim);
    g.setFont(Fonts::get().semibold(20.0f));
    g.drawText(juce::String(slot + 1), juce::Rectangle<int>(14, 7, 30, 26), juce::Justification::centredLeft, false);
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
        drawLabel(g, stateText, { 40, 11, getWidth() - 40 - 60, 18 }, textDim, juce::Justification::centred);

    // percentage = actual share of the blend (mute / solo aware)
    const bool loaded = isLoadedState(status.state);
    if (loaded)
    {
        g.setColour(audible ? text : textFaint);
        g.setFont(Fonts::get().medium(14.0f).withExtraKerningFactor(0.02f));
        g.drawText(juce::String(juce::roundToInt(effective)) + "%", juce::Rectangle<int>(getWidth() - 14 - 52, 10, 52, 20),
                   juce::Justification::centredRight, false);
    }

    // the filename - large, fixed size, wrapped, centred
    {
        // PAN row: label, centre tick behind the slider, value (C / L35 / R20)
        const bool loaded = isLoadedState(status.state);
        const auto pr = panArea();
        drawLabel(g, "PAN", pr.withWidth(32), loaded ? textDim : textFaint, juce::Justification::centredLeft);
        const auto sb = panSlider.getBounds();
        g.setColour(line);
        g.fillRect(sb.getCentreX(), sb.getY() + 3, 1, sb.getHeight() - 6);
        const int v = juce::roundToInt(panSlider.getValue());
        const juce::String t = v == 0 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String(std::abs(v));
        g.setColour(loaded ? (v == 0 ? textDim : text) : textFaint);
        g.setFont(Fonts::get().medium(12.0f));
        g.drawText(t, pr.withTrimmedLeft(pr.getWidth() - 32), juce::Justification::centredRight, false);
    }

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

    // the cabinet IR, if any, in one quiet line under the capture name
    if (irStatus.state != AmpsurdProcessor::IrState::none)
    {
        juce::String t;
        switch (irStatus.state)
        {
            case AmpsurdProcessor::IrState::loading: t = "IR: loading..."; break;
            case AmpsurdProcessor::IrState::missing: t = "IR file missing: " + irStatus.fileName; break;
            case AmpsurdProcessor::IrState::error:   t = "IR cannot load: " + irStatus.fileName; break;
            default: t = (irOn ? "+ IR  " : "IR off  ") + irStatus.fileName; break;
        }
        g.setColour(irOn && irStatus.state == AmpsurdProcessor::IrState::loaded ? textDim : textFaint);
        g.setFont(Fonts::get().regular(12.0f));
        g.drawFittedText(t, irLineArea(), juce::Justification::centred, 1, 0.9f);
    }
}

void SlotComponent::refresh(bool isSelected, bool irEditorOpen)
{
    const auto st = proc.getSlotStatus(slot);
    const auto ir = proc.getIrStatus(slot);
    const bool on = proc.isIrOn(slot);
    const bool irChanged = ir.state != irStatus.state || ir.path != irStatus.path || on != irOn;
    irStatus = ir;
    irOn = on;
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
    panSlider.setEnabled(loaded);
    editButton.setToggleState(selected && !irEditorOpen, juce::dontSendNotification);

    // IR button: ADD IR / IR (lit = loaded and on) / IR OFF / IR ?
    juce::String irText = "ADD IR";
    bool lit = false;
    switch (ir.state)
    {
        case AmpsurdProcessor::IrState::none: break;
        case AmpsurdProcessor::IrState::loading: irText = "IR..."; break;
        case AmpsurdProcessor::IrState::loaded: irText = on ? "IR" : "IR OFF"; lit = on; break;
        case AmpsurdProcessor::IrState::missing:
        case AmpsurdProcessor::IrState::error: irText = "IR ?"; break;
    }
    if (irButton.getButtonText() != irText) irButton.setButtonText(irText);
    if ((bool) irButton.getProperties().getWithDefault("lit", false) != lit)
    {
        irButton.getProperties().set("lit", lit);
        irButton.repaint();
    }
    irButton.setEnabled(st.state != AmpsurdProcessor::SlotState::empty);
    irButton.setToggleState(irEditorOpen && selected, juce::dontSendNotification); // open = filled, like EDIT
    removeButton.setEnabled(st.state != AmpsurdProcessor::SlotState::empty);
    if (removeArmedAt > 0.0 && juce::Time::getMillisecondCounterHiRes() - removeArmedAt > 3000.0)
    {
        removeArmedAt = -1.0;
        removeButton.setButtonText("REMOVE");
    }

    juce::String tip = st.state == AmpsurdProcessor::SlotState::empty ? juce::String("Empty slot") : st.info;
    if (st.state != AmpsurdProcessor::SlotState::empty) tip = st.path + "\n" + st.info.fromFirstOccurrenceOf("\n", false, false);
    if (tip != lastTooltip) { setTooltip(tip); lastTooltip = tip; }

    fader.repaint();
    if (changed || irChanged) repaint();
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
    if (files.size() != 1) return false;
    const juce::File f(files[0]);
    return f.hasFileExtension(".nam")
        || (f.hasFileExtension(".wav;.aif;.aiff;.flac") && proc.getSlotStatus(slot).state != AmpsurdProcessor::SlotState::empty);
}

void SlotComponent::filesDropped(const juce::StringArray& files, int, int)
{
    dragHover = false;
    if (!files.isEmpty())
    {
        const juce::File f(files[0]);
        if (f.hasFileExtension(".nam")) proc.loadCapture(slot, f);
        else proc.loadIr(slot, f); // an audio file dropped on a slot = its cabinet IR
    }
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
    if (slot < AmpsurdProcessor::kGlobalEq && proc.isGlobalEqOn())
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
        g.drawText(r.getWidth() > 760 ? "Drag a point: left/right = frequency, up/down = gain.  Wheel = width (Q) of the nearest point.  Double-click = reset."
                                      : "Drag: frequency / gain.  Wheel: Q.  Double-click: reset.",
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
    if (slot < AmpsurdProcessor::kGlobalEq)
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
    removeButton.setTooltip("Remove the capture (and its IR) from this slot");
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
// FxSection: delays, reverb, flanger
// =============================================================================================
FxSection::Row FxSection::makeRow(const juce::String& label, const juce::String& paramId)
{
    Row r;
    r.label = label;
    r.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    r.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 86, 22);
    r.slider->setTextBoxIsEditable(true); // click the value and type it, e.g. 756
    r.slider->setTooltip(label + ": drag, or click the value and type it");
    addChildComponent(*r.slider);
    r.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params, paramId, *r.slider);
    return r;
}

std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> FxSection::attachButton(juce::Button& b, const juce::String& id)
{
    b.setClickingTogglesState(true);
    addChildComponent(b);
    return std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params, id, b);
}

FxSection::FxSection(AmpsurdProcessor& p) : proc(p)
{
    const char* names[4] = { "DELAY 1", "DELAY 2", "REVERB", "FLANGER" };
    for (int i = 0; i < 4; ++i)
    {
        selector[(size_t) i].setButtonText(names[i]);
        selector[(size_t) i].onClick = [this, i] { select(i); };
        selector[(size_t) i].setTooltip("Show this effect's controls (lit = the effect is on)");
        addAndMakeVisible(selector[(size_t) i]);
    }
    for (int d = 0; d < 2; ++d)
    {
        const juce::String id = "fxD" + juce::String(d + 1);
        dOn[(size_t) d].setButtonText("ON");
        dSync[(size_t) d].setButtonText("SYNC");
        dPing[(size_t) d].setButtonText("PING-PONG");
        buttonAtts.push_back(attachButton(dOn[(size_t) d], id + "On"));
        buttonAtts.push_back(attachButton(dSync[(size_t) d], id + "Sync"));
        buttonAtts.push_back(attachButton(dPing[(size_t) d], id + "PingPong"));
        dSync[(size_t) d].setTooltip("SYNC: delay time as a note value of the tempo");
        dPing[(size_t) d].setTooltip("PING-PONG: repeats alternate left / right");
        dNote[(size_t) d].addItemList(AmpsurdProcessor::delayNoteNames(), 1);
        addChildComponent(dNote[(size_t) d]);
        noteAtts[(size_t) d] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params, id + "Note", dNote[(size_t) d]);
        dRows[(size_t) d][0] = makeRow("TIME", id + "Time");
        dRows[(size_t) d][1] = makeRow("FEEDBACK", id + "Feedback");
        dRows[(size_t) d][2] = makeRow("LEVEL", id + "Level");
        dRows[(size_t) d][3] = makeRow("TONE", id + "Tone");
    }
    tempoRow = makeRow("TEMPO", "tempo");
    tapButton.onClick = [this] { tap(); };
    tapButton.setTooltip("Tap the tempo (at least two taps)");
    addChildComponent(tapButton);

    rOn.setButtonText("ON");
    buttonAtts.push_back(attachButton(rOn, "fxRevOn"));
    for (int t = 0; t < ampsurd::kNumReverbTypes; ++t)
    {
        rType[(size_t) t].setButtonText(ampsurd::reverbTypeName((ampsurd::ReverbType) t));
        rType[(size_t) t].onClick = [this, t] {
            // a type sets its typical decay, pre-delay and tone (all adjustable afterwards)
            setParamValue(proc, "fxRevType", (float) t);
            const auto d = ampsurd::reverbTypeDefaults((ampsurd::ReverbType) t);
            setParamValue(proc, "fxRevDecay", d.decaySeconds);
            setParamValue(proc, "fxRevPreDelay", d.preDelayMs);
            setParamValue(proc, "fxRevTone", d.toneHz);
        };
        addChildComponent(rType[(size_t) t]);
    }
    rRows[0] = makeRow("DECAY", "fxRevDecay");
    rRows[1] = makeRow("PRE-DELAY", "fxRevPreDelay");
    rRows[2] = makeRow("TONE", "fxRevTone");
    rRows[3] = makeRow("LEVEL", "fxRevLevel");

    fOn.setButtonText("ON");
    buttonAtts.push_back(attachButton(fOn, "fxFlOn"));
    fRows[0] = makeRow("RATE", "fxFlRate");
    fRows[1] = makeRow("DEPTH", "fxFlDepth");
    fRows[2] = makeRow("FEEDBACK", "fxFlFeedback");
    fRows[3] = makeRow("MIX", "fxFlMix");
    select(0);
}

void FxSection::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (!taps.empty() && now - taps.back() > 2000.0) taps.clear();
    taps.push_back(now);
    if (taps.size() > 5) taps.erase(taps.begin());
    if (taps.size() >= 2)
    {
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        setParamValue(proc, "tempo", (float) juce::jlimit(40.0, 240.0, 60000.0 / avg));
    }
}

void FxSection::select(int which)
{
    selected = which;
    for (int i = 0; i < 4; ++i) selector[(size_t) i].setToggleState(i == which, juce::dontSendNotification);
    for (int d = 0; d < 2; ++d)
    {
        const bool v = which == d;
        for (juce::Component* c : { (juce::Component*) &dOn[(size_t) d], (juce::Component*) &dSync[(size_t) d],
                                    (juce::Component*) &dPing[(size_t) d], (juce::Component*) &dNote[(size_t) d] })
            c->setVisible(v);
        for (auto& r : dRows[(size_t) d]) r.slider->setVisible(v);
    }
    tempoRow.slider->setVisible(which < 2);
    tapButton.setVisible(which < 2);
    rOn.setVisible(which == 2);
    for (auto& b : rType) b.setVisible(which == 2);
    for (auto& r : rRows) r.slider->setVisible(which == 2);
    fOn.setVisible(which == 3);
    for (auto& r : fRows) r.slider->setVisible(which == 3);
    resized();
    refresh();
    repaint();
}

void FxSection::resized()
{
    auto r = getLocalBounds();
    auto sel = r.removeFromTop(26);
    const int sw = (sel.getWidth() - 3 * 6) / 4;
    for (int i = 0; i < 4; ++i)
    {
        selector[(size_t) i].setBounds(i == 3 ? sel : sel.removeFromLeft(sw));
        if (i < 3) sel.removeFromLeft(6);
    }
    r.removeFromTop(10);
    auto ctl = r.removeFromTop(26);
    r.removeFromTop(8);
    auto placeRows = [&](auto& rows, juce::Rectangle<int> area) {
        for (auto& row : rows)
        {
            auto line = area.removeFromTop(24);
            area.removeFromTop(6);
            row.slider->setBounds(line.withTrimmedLeft(84));
        }
    };
    if (selected < 2)
    {
        const int d = selected;
        auto c = ctl;
        dOn[(size_t) d].setBounds(c.removeFromLeft(56));
        c.removeFromLeft(8);
        dPing[(size_t) d].setBounds(c.removeFromLeft(96));
        c.removeFromLeft(8);
        dSync[(size_t) d].setBounds(c.removeFromLeft(60));
        c.removeFromLeft(8);
        dNote[(size_t) d].setBounds(c.removeFromLeft(118));
        placeRows(dRows[(size_t) d], r);
        auto t = r.withTrimmedTop(4 * 30).removeFromTop(24);
        tapButton.setBounds(t.removeFromRight(48));
        t.removeFromRight(8);
        tempoRow.slider->setBounds(t.withTrimmedLeft(84));
    }
    else if (selected == 2)
    {
        auto c = ctl;
        rOn.setBounds(c.removeFromLeft(56));
        c.removeFromLeft(8);
        const int bw = (c.getWidth() - 4 * 4) / 5;
        for (int t = 0; t < 5; ++t)
        {
            rType[(size_t) t].setBounds(t == 4 ? c : c.removeFromLeft(bw));
            if (t < 4) c.removeFromLeft(4);
        }
        placeRows(rRows, r);
    }
    else
    {
        fOn.setBounds(ctl.removeFromLeft(56));
        placeRows(fRows, r);
    }
}

void FxSection::paint(juce::Graphics& g)
{
    auto drawRowLabels = [&](auto& rows, bool enabled) {
        for (auto& row : rows)
            if (row.slider->isVisible())
                drawLabel(g, row.label, { 0, row.slider->getY(), 82, row.slider->getHeight() }, enabled ? textDim : textFaint);
    };
    if (selected < 2)
    {
        const bool on = proc.isFxActive(selected);
        drawRowLabels(dRows[(size_t) selected], on);
        drawLabel(g, "TEMPO", { 0, tempoRow.slider->getY(), 82, 24 }, textDim);
        if (syncInfo.isNotEmpty())
        {
            g.setColour(textDim);
            g.setFont(Fonts::get().regular(12.0f));
            g.drawText(syncInfo, dNote[(size_t) selected].getBounds().withX(dNote[(size_t) selected].getRight() + 8).withRight(getWidth()),
                       juce::Justification::centredLeft, true);
        }
    }
    else if (selected == 2) drawRowLabels(rRows, proc.isFxActive(2));
    else drawRowLabels(fRows, proc.isFxActive(3));
}

void FxSection::refresh()
{
    for (int i = 0; i < 4; ++i)
    {
        const bool on = proc.isFxActive(i);
        if ((bool) selector[(size_t) i].getProperties().getWithDefault("lit", false) != on)
        {
            selector[(size_t) i].getProperties().set("lit", on);
            selector[(size_t) i].repaint();
            repaint();
        }
    }
    if (selected < 2)
    {
        const int d = selected;
        const bool sync = paramValue(proc, "fxD" + juce::String(d + 1) + "Sync") > 0.5f;
        dRows[(size_t) d][0].slider->setEnabled(!sync);
        dNote[(size_t) d].setEnabled(sync);
        const bool host = proc.hostProvidesTempo();
        tempoRow.slider->setEnabled(!host);
        tapButton.setEnabled(!host);
        juce::String info;
        if (sync)
        {
            const double ms = 60000.0 / proc.getTempoBpm()
                            * AmpsurdProcessor::delayNoteBeats((int) paramValue(proc, "fxD" + juce::String(d + 1) + "Note"));
            info = "= " + juce::String(ms, 1) + " ms" + (host ? "  (host tempo " + juce::String(proc.getTempoBpm(), 1) + ")" : juce::String());
        }
        else if (host)
            info = "host tempo " + juce::String(proc.getTempoBpm(), 1) + " BPM";
        if (info != syncInfo) { syncInfo = info; repaint(); }
    }
    else if (selected == 2)
    {
        const int type = (int) paramValue(proc, "fxRevType");
        for (int t = 0; t < 5; ++t) rType[(size_t) t].setToggleState(t == type, juce::dontSendNotification);
    }
}

// =============================================================================================
// GlobalEqPanel: the same EQ as the amps, on the complete blend; no alignment.
// =============================================================================================
GlobalEqPanel::GlobalEqPanel(AmpsurdProcessor& p) : proc(p), graph(p), fx(p)
{
    addAndMakeVisible(graph);
    addAndMakeVisible(fx);
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
    r.removeFromTop(10);
    auto left = r.removeFromLeft((int) (r.getWidth() * 0.54f));
    r.removeFromLeft(24);
    fx.setBounds(r);
    auto eqHeader = left.removeFromTop(26);
    flatButton.setBounds(eqHeader.removeFromRight(64));
    eqHeader.removeFromRight(8);
    eqOnButton.setBounds(eqHeader.removeFromRight(64));
    left.removeFromTop(8);
    graph.setBounds(left);
}

void GlobalEqPanel::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(26);
    drawLabel(g, "GLOBAL EQ / FX", header.removeFromLeft(140), text, juce::Justification::centredLeft, 11.0f);
    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawText("For the complete sound:  blend  >  EQ  >  gate  >  flanger  >  delays  >  reverb  >  OUTPUT",
               header.withTrimmedRight(90), juce::Justification::centredLeft, true);
    r.removeFromTop(10);
    drawLabel(g, "EQ", r.removeFromTop(26).removeFromLeft(60), proc.isGlobalEqOn() ? text : textFaint);
}

void GlobalEqPanel::refresh()
{
    graph.refreshIfChanged();
    fx.refresh();
}

// =============================================================================================
// PlayerPanel
// =============================================================================================
namespace
{
juce::String clock(double seconds)
{
    const int total = (int) std::floor(seconds * 10.0);
    return juce::String(total / 600).paddedLeft('0', 2) + ":" + juce::String((total / 10) % 60).paddedLeft('0', 2) + "." + juce::String(total % 10);
}
} // namespace

// ---------------------------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------------------------
Timeline::Timeline(PlayerRecorder& p) : player(p)
{
    setRepaintsOnMouseActivity(false);
}

juce::Rectangle<int> Timeline::backingLane() const
{
    auto r = laneArea().withTrimmedTop(18);
    return r.removeFromTop((r.getHeight() - 6) * 2 / 5);
}

juce::Rectangle<int> Timeline::guitarLane() const
{
    auto r = laneArea().withTrimmedTop(18);
    r.removeFromTop((r.getHeight() - 6) * 2 / 5 + 6);
    return r;
}

void Timeline::clampView()
{
    const double len = std::max(10.0, player.getLengthSeconds() + 5.0);
    const double w = laneArea().getWidth();
    secondsPerPixel = juce::jlimit(0.0002, std::max(0.0002, len * 1.05 / std::max(1.0, w)), secondsPerPixel); // ~10 ms .. whole song
    viewStart = juce::jlimit(0.0, std::max(0.0, len - secondsPerPixel * w), viewStart);
}

void Timeline::fit()
{
    const double len = std::max(10.0, player.getLengthSeconds());
    viewStart = 0.0;
    secondsPerPixel = len * 1.03 / std::max(1, laneArea().getWidth());
    fitted = true;
    repaint();
}

void Timeline::zoom(double factor, double around)
{
    const double ax = (around - viewStart) / secondsPerPixel;
    secondsPerPixel *= factor;
    viewStart = around - ax * secondsPerPixel;
    clampView();
    repaint();
}

void Timeline::followPlayhead()
{
    if (!fitted && laneArea().getWidth() > 0) fit();
    using S = PlayerRecorder::State;
    const auto st = player.getState();
    const double pos = player.getPositionSeconds();
    const double span = secondsPerPixel * laneArea().getWidth();
    if ((st == S::playing || st == S::recording) && drag.clip < 0 && (pos > viewStart + span * 0.95 || pos < viewStart))
        viewStart = std::max(0.0, pos - span * 0.05); // page along with the playhead
}

Timeline::EdgeHit Timeline::hitEdge(juce::Point<int> p) const
{
    if (!guitarLane().expanded(0, 2).contains(p)) return {};
    const auto& clips = player.getClips();
    const double sr = player.getSampleRate();
    for (int i = (int) clips.size() - 1; i >= 0; --i) // the latest clip is on top
    {
        if (std::abs(timeToX((double) clips[(size_t) i].in / sr) - (float) p.x) <= 5.0f) return { i, true };
        if (std::abs(timeToX((double) clips[(size_t) i].out / sr) - (float) p.x) <= 5.0f) return { i, false };
    }
    return {};
}

void Timeline::mouseMove(const juce::MouseEvent& e)
{
    const auto h = hitEdge(e.getPosition());
    if (h.clip != hover.clip || h.start != hover.start)
    {
        hover = h;
        setMouseCursor(h.clip >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void Timeline::mouseExit(const juce::MouseEvent&)
{
    if (hover.clip >= 0) { hover = {}; repaint(); }
}

void Timeline::mouseDown(const juce::MouseEvent& e)
{
    using S = PlayerRecorder::State;
    const bool recording = player.getState() == S::recording || player.getState() == S::pausedRec;
    const auto h = hitEdge(e.getPosition());
    if (e.mods.isPopupMenu())
    {
        // right-click on a correction: remove it
        const auto& clips = player.getClips();
        const double t = xToTime((float) e.x) * player.getSampleRate();
        for (int i = (int) clips.size() - 1; i >= 0 && !recording; --i)
            if (t >= (double) clips[(size_t) i].in && t < (double) clips[(size_t) i].out && guitarLane().contains(e.getPosition()))
            {
                juce::PopupMenu m;
                m.addItem(1, i == 0 && clips.size() == 1 ? "Remove the take" : "Remove this recording (" + juce::String(i + 1) + ")");
                m.showMenuAsync(juce::PopupMenu::Options(), [this, i](int r) { if (r == 1) player.removeClip(i); });
                return;
            }
        return;
    }
    if (h.clip >= 0 && !recording)
    {
        drag = h;
        const auto& c = player.getClips()[(size_t) h.clip];
        dragTime = (double) (h.start ? c.in : c.out) / player.getSampleRate();
        return;
    }
    if (!recording && laneArea().contains(e.getPosition()))
        player.setPositionSeconds(std::max(0.0, xToTime((float) e.x)));
}

void Timeline::mouseDrag(const juce::MouseEvent& e)
{
    if (drag.clip < 0) return;
    const auto& clips = player.getClips();
    if (drag.clip >= (int) clips.size()) { drag = {}; return; }
    const auto& c = clips[(size_t) drag.clip];
    const double sr = player.getSampleRate(), minLen = 0.05;
    double t = xToTime((float) e.x);
    if (drag.start) t = juce::jlimit((double) c.fileStart / sr, (double) c.out / sr - minLen, t);
    else t = juce::jlimit((double) c.in / sr + minLen, (double) c.fileEnd() / sr, t);
    dragTime = t;
    repaint();
}

void Timeline::mouseUp(const juce::MouseEvent&)
{
    if (drag.clip < 0) return;
    const auto& clips = player.getClips();
    if (drag.clip < (int) clips.size())
    {
        const auto& c = clips[(size_t) drag.clip];
        const auto pos = (juce::int64) std::llround(dragTime * player.getSampleRate());
        player.setClipEdges(drag.clip, drag.start ? pos : c.in, drag.start ? c.out : pos);
    }
    drag = {};
    repaint();
}

void Timeline::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isShiftDown() || std::abs(w.deltaX) > std::abs(w.deltaY))
    {
        const float d = std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY;
        viewStart -= (double) d * 300.0 * secondsPerPixel;
        clampView();
        repaint();
        return;
    }
    zoom(w.deltaY > 0 ? 0.8 : 1.25, xToTime((float) e.x));
}

void Timeline::paint(juce::Graphics& g)
{
    clampView();
    const double sr = player.getSampleRate();
    const auto lanes = laneArea();
    const double v0 = viewStart, v1 = viewStart + secondsPerPixel * lanes.getWidth();

    // labels
    g.setColour(textDim);
    g.setFont(Fonts::get().semibold(10.5f));
    g.drawText("BACKING", backingLane().withX(0).withWidth(58), juce::Justification::centredLeft, false);
    g.drawText("GUITAR", guitarLane().withX(0).withWidth(58), juce::Justification::centredLeft, false);

    // ruler: a tick step that leaves >= 70 px between labels
    {
        const auto ruler = rulerArea();
        const double steps[] = { 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 };
        double step = 300;
        for (double s2 : steps) if (s2 / secondsPerPixel >= 70.0) { step = s2; break; }
        g.setFont(Fonts::get().regular(10.5f));
        for (double t = std::floor(v0 / step) * step; t <= v1; t += step)
        {
            const float x = timeToX(t);
            if (x < (float) lanes.getX()) continue;
            g.setColour(grid);
            g.drawVerticalLine((int) x, (float) ruler.getBottom(), (float) lanes.getBottom());
            g.setColour(textFaint);
            const int m = (int) (t / 60.0);
            const double sec = t - m * 60.0;
            const juce::String label = juce::String(m) + ":" + (step < 1.0 ? juce::String(sec, step < 0.1 ? 2 : 1).paddedLeft('0', step < 0.1 ? 5 : 4)
                                                                            : juce::String((int) std::lround(sec)).paddedLeft('0', 2));
            g.drawText(label, juce::Rectangle<float>(x + 3.0f, (float) ruler.getY(), 60.0f, (float) ruler.getHeight()), juce::Justification::centredLeft, false);
        }
    }

    auto drawLane = [&](juce::Rectangle<int> lane, juce::AudioThumbnail& th, double start, double length, juce::Colour c) {
        g.setColour(line);
        g.drawRect(lane, 1);
        if (length <= 0.0 || th.getTotalLength() <= 0.0) return;
        const double a = std::max(v0, start), b = std::min(v1, start + length);
        if (b <= a) return;
        const auto area = juce::Rectangle<int>((int) timeToX(a), lane.getY() + 2, std::max(1, (int) (timeToX(b) - timeToX(a))), lane.getHeight() - 4);
        g.setColour(c);
        th.drawChannel(g, area, a - start, b - start, 0, 1.0f);
    };

    // backing (starts at 0)
    drawLane(backingLane(), player.getBackingThumbnail(), 0.0, player.getBackingLengthSeconds(), textFaint);

    // guitar: the composite take
    const auto glane = guitarLane();
    drawLane(glane, player.getTakeThumbnail(), player.getTakeStartSeconds(), player.getTakeLengthSeconds(), textDim);

    // while recording: the new audio from the punch-in on
    using S = PlayerRecorder::State;
    const auto st = player.getState();
    if ((st == S::recording || st == S::pausedRec) && player.getPassStart() >= 0)
    {
        const double ps = (double) player.getPassStart() / sr;
        const double from = player.getPunchIn() >= 0 ? (double) player.getPunchIn() / sr : ps;
        auto& th = player.getPassThumbnail();
        const double a = std::max(v0, from), b = std::min(v1, ps + th.getTotalLength());
        if (b > a)
        {
            const auto area = juce::Rectangle<int>((int) timeToX(a), glane.getY() + 2, std::max(1, (int) (timeToX(b) - timeToX(a))), glane.getHeight() - 4);
            g.setColour(background);
            g.fillRect(area);
            g.setColour(text);
            th.drawChannel(g, area, a - ps, b - ps, 0, 1.0f);
        }
    }

    // clips: a bracket on top of the guitar lane with draggable start / end edges
    const auto& clips = player.getClips();
    for (int i = 0; i < (int) clips.size(); ++i)
    {
        const auto& c = clips[(size_t) i];
        double in = (double) c.in / sr, out = (double) c.out / sr;
        if (drag.clip == i) (drag.start ? in : out) = dragTime;
        if (out < v0 || in > v1) continue;
        const float x0 = std::max(timeToX(in), (float) lanes.getX()), x1 = std::min(timeToX(out), (float) lanes.getRight());
        const bool correction = i > 0;
        g.setColour((correction ? text : textFaint).withAlpha(0.08f));
        if (correction) g.fillRect(juce::Rectangle<float>(x0, (float) glane.getY() + 1.0f, x1 - x0, (float) glane.getHeight() - 2.0f));
        g.setColour(correction ? text : textFaint);
        g.fillRect(juce::Rectangle<float>(x0, (float) glane.getY(), x1 - x0, 3.0f));
        auto edge = [&](double t, bool isStart) {
            const float x = timeToX(t);
            if (x < (float) lanes.getX() || x > (float) lanes.getRight()) return;
            const bool hot = (hover.clip == i && hover.start == isStart) || (drag.clip == i && drag.start == isStart);
            g.setColour(hot ? text : (correction ? textDim : textFaint));
            g.drawLine(x, (float) glane.getY(), x, (float) glane.getBottom(), hot ? 2.0f : 1.0f);
            const float hx = isStart ? x : x - 8.0f;
            g.fillRect(juce::Rectangle<float>(hx, (float) glane.getY(), 8.0f, 10.0f));
        };
        edge(in, true);
        edge(out, false);
        if (correction && x1 - x0 > 24.0f)
        {
            g.setColour(textDim);
            g.setFont(Fonts::get().semibold(10.0f));
            g.drawText(juce::String(i + 1), juce::Rectangle<float>(x0 + 10.0f, (float) glane.getY() + 3.0f, 30.0f, 12.0f), juce::Justification::centredLeft, false);
        }
    }
    if (drag.clip >= 0)
    {
        g.setColour(text);
        g.setFont(Fonts::get().regular(11.0f));
        g.drawText(clock(dragTime), juce::Rectangle<float>(timeToX(dragTime) + 6.0f, (float) glane.getBottom() - 16.0f, 80.0f, 14.0f),
                   juce::Justification::centredLeft, false);
    }

    if (player.isRendering())
    {
        g.setColour(textFaint);
        g.setFont(Fonts::get().regular(11.0f));
        g.drawText("updating the track...", glane.reduced(8, 4), juce::Justification::bottomRight, false);
    }
    else if (player.getRenderError().isNotEmpty())
    {
        g.setColour(text);
        g.setFont(Fonts::get().regular(11.0f));
        g.drawText(player.getRenderError(), glane.reduced(8, 4), juce::Justification::bottomRight, false);
    }
    else if (!player.hasTake() && st != S::recording)
    {
        g.setColour(textFaint);
        g.setFont(Fonts::get().regular(12.0f));
        g.drawText("REC records the take. With a take: PLAY, then REC = punch in a correction, REC again = punch out. "
                   "Drag the edges of a recording to move its start / end - the joins are crossfaded.",
                   glane.reduced(10, 4), juce::Justification::centredLeft, true);
    }

    // playhead
    const float px = timeToX(player.getPositionSeconds());
    if (px >= (float) lanes.getX() && px <= (float) lanes.getRight())
    {
        g.setColour(text);
        g.drawLine(px, (float) rulerArea().getY(), px, (float) lanes.getBottom(), 1.5f);
    }
}

// ---------------------------------------------------------------------------------------------
// PlayerPanel
// ---------------------------------------------------------------------------------------------
PlayerPanel::PlayerPanel(AmpsurdProcessor& p, PlayerRecorder& pl) : proc(p), player(pl), timeline(pl)
{
    addAndMakeVisible(timeline);
    for (auto* b : { &startButton, &playButton, &pauseButton, &stopButton, &recButton, &undoButton, &clearButton, &zoomOutButton,
                     &zoomInButton, &fitButton, &loadButton, &removeButton, &saveButton, &bounceButton, &closeButton })
        addAndMakeVisible(*b);
    startButton.onClick = [this] { player.toStart(); };
    playButton.onClick = [this] { player.play(); };
    pauseButton.onClick = [this] { player.pause(); };
    stopButton.onClick = [this] { player.stop(); };
    recButton.onClick = [this] { player.record(); };
    undoButton.onClick = [this] { player.undo(); };
    clearButton.onClick = [this] {
        // removes the whole guitar track (UNDO brings it back) - click twice
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (!(clearArmedAt > 0.0 && now - clearArmedAt < 3000.0))
        {
            clearArmedAt = now;
            clearButton.setButtonText("SURE?");
            return;
        }
        clearArmedAt = -1.0;
        clearButton.setButtonText("CLEAR TAKE");
        player.clearTake();
    };
    zoomInButton.onClick = [this] { timeline.zoomIn(); };
    zoomOutButton.onClick = [this] { timeline.zoomOut(); };
    fitButton.onClick = [this] { timeline.fit(); };
    loadButton.onClick = [this] { loadBacking(); };
    removeButton.onClick = [this] { player.removeBacking(); };
    saveButton.onClick = [this] { saveAs(); };
    bounceButton.onClick = [this] {
        setStatus("Bouncing...");
        player.bounce([this](const juce::String& err) { setStatus(err.isEmpty() ? "Bounced: backing + take are the new backing track. Record the next layer." : err); });
    };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    startButton.setTooltip("Back to the start");
    recButton.setTooltip("No take yet: record it. With a take: punch in a correction here (the take is replaced from this point); "
                         "press again to punch out. Start PLAY a little earlier, then the correction's start can later be dragged earlier too.");
    pauseButton.setTooltip("Pause / continue (also while recording: the recording continues seamlessly)");
    undoButton.setTooltip("Undo the last recording or edge move");
    clearButton.setTooltip("Remove the whole guitar track (click twice; UNDO brings it back)");
    zoomInButton.setTooltip("Zoom in (or mouse wheel over the timeline; Shift + wheel scrolls)");
    zoomOutButton.setTooltip("Zoom out");
    fitButton.setTooltip("Show the whole song");
    bounceButton.setTooltip("Mix backing + take into a new backing track, to record another layer on top");
    saveButton.setTooltip("Save the recording (guitar alone, or with the backing track)");
    loadButton.setTooltip("Backing track or click track: WAV, FLAC, MP3, OGG, AIFF");

    auto setupDb = [this](juce::Slider& sl, double lo, double hi, std::function<void(float)> apply, double initial, const juce::String& unit) {
        sl.setSliderStyle(juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
        sl.setRange(lo, hi, 0.1);
        sl.setValue(initial, juce::dontSendNotification);
        sl.setTextValueSuffix(" " + unit);
        sl.setDoubleClickReturnValue(true, 0.0);
        sl.onValueChange = [&sl, apply] { apply((float) sl.getValue()); };
        addAndMakeVisible(sl);
    };
    setupDb(backVol, -40.0, 6.0, [this](float v) { player.setBackingGainDb(v); }, player.getBackingGainDb(), "dB");
    setupDb(takeVol, -40.0, 6.0, [this](float v) { player.setTakeGainDb(v); }, player.getTakeGainDb(), "dB");
    setupDb(offset, -50.0, 50.0, [this](float v) { player.setOffsetMs(v); }, player.getOffsetMs(), "ms");
    offset.setTooltip("Fine-tune where recordings land, if your audio driver reports its latency slightly wrong");

    formatBox.addItemList(PlayerRecorder::formatNames(), 1);
    formatBox.setSelectedItemIndex(0, juce::dontSendNotification); // CD quality
    for (auto r : PlayerRecorder::exportRates()) rateBox.addItem(juce::String(r / 1000.0, 1) + " kHz", (int) r);
    rateBox.setSelectedItemIndex(0, juce::dontSendNotification);   // 44.1 kHz
    contentBox.addItem("GUITAR + BACKING", 1);
    contentBox.addItem("GUITAR ONLY", 2);
    contentBox.setSelectedId(1, juce::dontSendNotification);
    for (auto* c : { &formatBox, &rateBox, &contentBox }) addAndMakeVisible(*c);
}

void PlayerPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 10);
    auto header = r.removeFromTop(24);
    closeButton.setBounds(header.removeFromRight(72));
    r.removeFromTop(6);

    auto transport = r.removeFromTop(28);
    for (auto* b : { &startButton, &playButton, &pauseButton, &stopButton, &recButton })
    {
        b->setBounds(transport.removeFromLeft(b == &startButton ? 44 : 84));
        transport.removeFromLeft(6);
    }
    transport.removeFromLeft(14);
    undoButton.setBounds(transport.removeFromLeft(70));
    transport.removeFromLeft(6);
    clearButton.setBounds(transport.removeFromLeft(104));
    transport.removeFromLeft(14);
    fitButton.setBounds(transport.removeFromRight(52));
    transport.removeFromRight(6);
    zoomInButton.setBounds(transport.removeFromRight(30));
    transport.removeFromRight(4);
    zoomOutButton.setBounds(transport.removeFromRight(30));
    transport.removeFromRight(14);
    infoArea = transport;

    auto save = r.removeFromBottom(26);
    r.removeFromBottom(6);
    auto mix = r.removeFromBottom(26);
    r.removeFromBottom(8);
    r.removeFromTop(6);
    timeline.setBounds(r);

    // row: backing + guitar volume / offset
    {
        auto row = mix;
        loadButton.setBounds(row.removeFromLeft(118));
        row.removeFromLeft(6);
        removeButton.setBounds(row.removeFromLeft(76));
        row.removeFromLeft(10);
        backVol.setBounds(row.removeFromLeft(190).withTrimmedLeft(34));
        backMeter = row.removeFromLeft(60).withSizeKeepingCentre(60, 8);
        row.removeFromLeft(24);
        takeVol.setBounds(row.removeFromLeft(230).withTrimmedLeft(74));
        takeMeter = row.removeFromLeft(60).withSizeKeepingCentre(60, 8);
        row.removeFromLeft(24);
        offset.setBounds(row.removeFromLeft(220).withTrimmedLeft(52));
    }
    // row: save
    {
        auto row = save;
        formatBox.setBounds(row.removeFromLeft(160));
        row.removeFromLeft(6);
        rateBox.setBounds(row.removeFromLeft(90));
        row.removeFromLeft(6);
        contentBox.setBounds(row.removeFromLeft(170));
        row.removeFromLeft(6);
        saveButton.setBounds(row.removeFromLeft(100));
        row.removeFromLeft(6);
        bounceButton.setBounds(row.removeFromLeft(84));
        row.removeFromLeft(12);
        statusArea = row;
    }
}

void PlayerPanel::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    auto r = getLocalBounds().reduced(16, 10);
    auto header = r.removeFromTop(24);
    drawLabel(g, "PLAYER / RECORDER", header.removeFromLeft(160), text, juce::Justification::centredLeft, 11.0f);

    using S = PlayerRecorder::State;
    const auto st = player.getState();
    const juce::String stateText = st == S::playing ? "PLAYING" : st == S::recording ? "RECORDING" : st == S::pausedPlay ? "PAUSED"
                                 : st == S::pausedRec ? "RECORDING PAUSED" : "STOPPED";
    g.setColour(text);
    g.setFont(Fonts::get().semibold(17.0f));
    g.drawText(clock(player.getPositionSeconds()) + "  /  " + clock(player.getLengthSeconds()), header.removeFromLeft(220), juce::Justification::centredLeft, false);
    drawLabel(g, (st == S::recording ? juce::String::charToString((juce::juce_wchar) 0x25CF) + " " : juce::String()) + stateText,
              header.removeFromLeft(170), st == S::recording ? text : textDim, juce::Justification::centredLeft);
    g.setColour(textFaint);
    g.setFont(Fonts::get().regular(11.5f));
    g.drawFittedText((player.hasBacking() ? "Backing: " + player.getBackingName() + "   " : juce::String())
                         + "Recordings are lined up automatically (" + juce::String(player.getCompensationMs(), 1) + " ms)",
                     header.withTrimmedRight(88), juce::Justification::centredRight, 1, 0.8f);

    // labels of the control rows
    g.setColour(textDim);
    g.setFont(Fonts::get().semibold(10.5f));
    g.drawText("VOL", backVol.getBounds().withX(backVol.getX() - 34).withWidth(30), juce::Justification::centredLeft, false);
    g.drawText("GUITAR VOL", takeVol.getBounds().withX(takeVol.getX() - 74).withWidth(72), juce::Justification::centredLeft, false);
    g.drawText("OFFSET", offset.getBounds().withX(offset.getX() - 52).withWidth(50), juce::Justification::centredLeft, false);

    auto meter = [&](juce::Rectangle<int> m, float lvl) {
        g.setColour(line);
        g.drawRect(m.toFloat(), 1.0f);
        const float db = juce::Decibels::gainToDecibels(lvl, -60.0f);
        const float w = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f) * (float) (m.getWidth() - 2);
        g.setColour(textDim);
        g.fillRect((float) m.getX() + 1.0f, (float) m.getY() + 1.0f, w, (float) m.getHeight() - 2.0f);
    };
    meter(backMeter, backLevel);
    meter(takeMeter, takeLevel);

    g.setColour(textFaint);
    g.setFont(Fonts::get().regular(11.5f));
    const juce::String hint = st == S::recording ? (player.getClips().empty() && !player.hasTake() ? "Recording the take..." : "Recording a correction - REC again = punch out")
                            : player.hasTake() ? "REC = punch in a correction at the playhead. Drag recording edges to move them."
                                               : "REC records your guitar along with the backing track.";
    g.drawFittedText(hint, infoArea, juce::Justification::centredLeft, 2, 0.8f);
    const juce::String st2 = player.isBusy() ? "Working... " + juce::String(juce::roundToInt(player.getProgress() * 100.0f)) + " %" : status;
    g.drawFittedText(st2.isNotEmpty() ? st2 : "Recordings: Documents / AMPSURD / Recordings", statusArea, juce::Justification::centredLeft, 2, 0.8f);
}

void PlayerPanel::refresh()
{
    using S = PlayerRecorder::State;
    player.poll();
    const auto st = player.getState();
    const bool rec = st == S::recording || st == S::pausedRec;
    playButton.setToggleState(st == S::playing, juce::dontSendNotification);
    recButton.setToggleState(rec, juce::dontSendNotification);
    recButton.setButtonText(st == S::recording && (player.hasTake() || !player.getClips().empty()) ? "PUNCH OUT" : "REC");
    pauseButton.setToggleState(st == S::pausedPlay || st == S::pausedRec, juce::dontSendNotification);
    pauseButton.setButtonText(st == S::pausedPlay || st == S::pausedRec ? "CONTINUE" : "PAUSE");
    const bool busy = player.isBusy();
    for (auto* b : { &saveButton, &bounceButton })
        b->setEnabled(!busy && !rec && player.hasTake() && !player.isRendering());
    loadButton.setEnabled(!rec && !busy);
    removeButton.setEnabled(!rec && !busy && player.hasBacking());
    undoButton.setEnabled(!rec && player.canUndo());
    clearButton.setEnabled(!rec && (player.hasTake() || !player.getClips().empty()));
    if (clearArmedAt > 0.0 && juce::Time::getMillisecondCounterHiRes() - clearArmedAt > 3000.0)
    {
        clearArmedAt = -1.0;
        clearButton.setButtonText("CLEAR TAKE");
    }
    backLevel = std::max(player.getAndResetBackingPeak(), backLevel * 0.85f);
    takeLevel = std::max(player.getAndResetTakePeak(), takeLevel * 0.85f);
    timeline.followPlayhead();
    timeline.repaint();
    repaint();
}

void PlayerPanel::loadBacking()
{
    chooser = std::make_unique<juce::FileChooser>("Load a backing track", juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                  "*.wav;*.flac;*.mp3;*.ogg;*.aif;*.aiff");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (!f.existsAsFile()) return;
        const auto err = player.loadBacking(f);
        setStatus(err.isEmpty() ? "Backing track loaded." : err);
    });
}

void PlayerPanel::saveAs()
{
    PlayerRecorder::ExportOptions o;
    o.format = formatBox.getSelectedItemIndex();
    o.rate = (double) rateBox.getSelectedId();
    o.withBacking = contentBox.getSelectedId() == 1;
    const bool flac = o.format >= 3;
    auto folder = player.getRecordingsFolder();
    folder.createDirectory();
    const auto suggested = folder.getNonexistentChildFile(o.withBacking ? "AMPSURD mix" : "AMPSURD guitar", flac ? ".flac" : ".wav");
    chooser = std::make_unique<juce::FileChooser>("Save the recording", suggested, flac ? "*.flac" : "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, o, flac](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f == juce::File()) return;
                             f = f.withFileExtension(flac ? ".flac" : ".wav");
                             setStatus("Saving...");
                             player.exportAudio(f, o, [this, f](const juce::String& err) {
                                 setStatus(err.isEmpty() ? "Saved: " + f.getFileName() : err);
                             });
                         });
}

// =============================================================================================
// IrPanel
// =============================================================================================
IrPanel::IrPanel(AmpsurdProcessor& p) : proc(p), graph(p)
{
    addAndMakeVisible(graph);
    for (auto* b : { &loadButton, &irOnButton, &removeButton, &eqOnButton, &flatButton, &closeButton })
        addAndMakeVisible(*b);
    irOnButton.setClickingTogglesState(true);
    eqOnButton.setClickingTogglesState(true);
    loadButton.onClick = [this] { chooseFile(); };
    removeButton.onClick = [this] { if (slot >= 0) proc.removeIr(slot); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    flatButton.onClick = [this] {
        if (slot < 0) return;
        const int t = AmpsurdProcessor::irEqTarget(slot);
        const auto d = ampsurd::ParametricEq::defaultBands();
        for (int b = 0; b < ampsurd::ParametricEq::kNumBands; ++b)
        {
            setParamValue(proc, AmpsurdProcessor::bandParamId(t, b, "gain"), 0.0f);
            setParamValue(proc, AmpsurdProcessor::bandParamId(t, b, "freq"), d[(size_t) b].freqHz);
            setParamValue(proc, AmpsurdProcessor::bandParamId(t, b, "q"), d[(size_t) b].q);
        }
    };
    loadButton.setTooltip("Load a cabinet impulse response (WAV, AIFF or FLAC) - or drop the file here");
    irOnButton.setTooltip("IR ON / bypass (bypassing the IR also bypasses its EQ)");
    removeButton.setTooltip("Remove the IR from this amp");
    eqOnButton.setTooltip("Switch this IR's EQ on or off");
    flatButton.setTooltip("Reset all ten bands of this IR's EQ");
    closeButton.setTooltip("Close the IR editor and return to the gate and tuner");
    setSlot(-1);
}

void IrPanel::setSlot(int s)
{
    slot = s;
    irOnAtt.reset();
    eqOnAtt.reset();
    graph.setSlot(s >= 0 ? AmpsurdProcessor::irEqTarget(s) : -1);
    if (s >= 0)
    {
        irOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params, AmpsurdProcessor::slotParamId(s, "irOn"), irOnButton);
        eqOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            proc.params, AmpsurdProcessor::slotParamId(AmpsurdProcessor::irEqTarget(s), "eqOn"), eqOnButton);
    }
    shown = {};
    refresh();
    repaint();
}

void IrPanel::resized()
{
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(26);
    closeButton.setBounds(header.removeFromRight(72));
    r.removeFromTop(10);
    auto left = r.removeFromLeft(r.getWidth() / 2 - 12);
    r.removeFromLeft(24);
    // right: IR EQ
    auto eqHeader = r.removeFromTop(26);
    flatButton.setBounds(eqHeader.removeFromRight(64));
    eqHeader.removeFromRight(8);
    eqOnButton.setBounds(eqHeader.removeFromRight(64));
    r.removeFromTop(8);
    graph.setBounds(r);
    // left: buttons at the bottom
    auto buttons = left.removeFromBottom(28);
    const int bw = (buttons.getWidth() - 16) / 3;
    loadButton.setBounds(buttons.removeFromLeft(bw));
    buttons.removeFromLeft(8);
    irOnButton.setBounds(buttons.removeFromLeft(bw));
    buttons.removeFromLeft(8);
    removeButton.setBounds(buttons);
}

void IrPanel::paint(juce::Graphics& g)
{
    g.setColour(line);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    if (slot < 0) return;
    auto r = getLocalBounds().reduced(16, 12);
    auto header = r.removeFromTop(26);
    drawLabel(g, "IR  /  AMP " + juce::String(slot + 1), header.removeFromLeft(130), text, juce::Justification::centredLeft, 11.0f);
    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawText(namName, header.withTrimmedRight(90), juce::Justification::centredLeft, true);
    r.removeFromTop(10);
    auto left = r.removeFromLeft(r.getWidth() / 2 - 12);
    r.removeFromLeft(24);

    // right header
    const bool irActive = shown.state == AmpsurdProcessor::IrState::loaded && proc.isIrOn(slot);
    drawLabel(g, "IR EQ", r.removeFromTop(26).withTrimmedRight(150), irActive ? text : textFaint, juce::Justification::centredLeft, 11.0f);

    // left: the IR
    left.removeFromBottom(28 + 14);
    drawLabel(g, "CABINET IR", left.removeFromTop(18), textDim);
    left.removeFromTop(8);
    using S = AmpsurdProcessor::IrState;
    if (shown.state == S::none)
    {
        g.setColour(textFaint);
        g.setFont(Fonts::get().regular(13.0f));
        g.drawFittedText("No IR loaded.\n\nFor captures of an amp WITHOUT its cabinet: load a cabinet impulse response "
                         "(WAV, AIFF or FLAC), or drop the file here or on the slot.\nFull-rig captures need no IR.",
                         left, juce::Justification::topLeft, 6);
        return;
    }
    FilenameLayout::draw(g, shown.fileName, left.removeFromTop(64).toFloat(), shown.state == S::loaded && proc.isIrOn(slot) ? text : textFaint);
    left.removeFromTop(8);
    juce::String info = shown.info;
    if (shown.state == S::missing) info = "File not found:\n" + shown.path + "\nLoad it again (or the file it was replaced by).";
    if (shown.state == S::loaded && !proc.isIrOn(slot)) info = "BYPASSED (IR and IR EQ)\n" + info;
    g.setColour(textDim);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawFittedText(info, left, juce::Justification::topLeft, 6);
}

void IrPanel::refresh()
{
    if (slot < 0) return;
    const auto st = proc.getIrStatus(slot);
    const auto nam = proc.getSlotStatus(slot).fileName;
    const bool on = proc.isIrOn(slot);
    if (st.state != shown.state || st.path != shown.path || st.info != shown.info || nam != namName
        || on != (bool) getProperties().getWithDefault("on", true))
    {
        shown = st;
        namName = nam;
        getProperties().set("on", on);
        const bool has = st.state != AmpsurdProcessor::IrState::none;
        loadButton.setButtonText(has ? "REPLACE IR" : "LOAD IR");
        irOnButton.setEnabled(st.state == AmpsurdProcessor::IrState::loaded);
        removeButton.setEnabled(has);
        const bool eqUsable = st.state == AmpsurdProcessor::IrState::loaded && on;
        eqOnButton.setEnabled(eqUsable);
        flatButton.setEnabled(st.state == AmpsurdProcessor::IrState::loaded);
        graph.setAlpha(eqUsable ? 1.0f : 0.45f);
        repaint();
    }
    graph.refreshIfChanged();
}

void IrPanel::chooseFile()
{
    if (slot < 0) return;
    auto start = juce::File(proc.getIrStatus(slot).path).getParentDirectory();
    if (!start.isDirectory()) start = juce::File(proc.getSlotStatus(slot).path).getParentDirectory();
    chooser = std::make_unique<juce::FileChooser>("Load cabinet IR for amp " + juce::String(slot + 1), start, "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             const auto f = fc.getResult();
                             if (f.existsAsFile() && slot >= 0) proc.loadIr(slot, f);
                         });
}

bool IrPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    return slot >= 0 && files.size() == 1 && juce::File(files[0]).hasFileExtension(".wav;.aif;.aiff;.flac");
}

void IrPanel::filesDropped(const juce::StringArray& files, int, int)
{
    if (slot >= 0 && !files.isEmpty()) proc.loadIr(slot, juce::File(files[0]));
}

// ---------------------------------------------------------------------------------------------
GlobalEqButton::GlobalEqButton(AmpsurdProcessor& p) : proc(p)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Open the Global EQ and the effects (delays, reverb, flanger) for the complete sound");
}

void GlobalEqButton::refresh()
{
    const bool eq = proc.isGlobalEqOn();
    const bool flat = ampsurd::ParametricEq::isFlat(proc.getEqBands(AmpsurdProcessor::kGlobalEq));
    juce::StringArray parts;
    if (eq) parts.add(flat ? "EQ (FLAT)" : "EQ");
    if (proc.isFxActive(3)) parts.add("FLG");
    if (proc.isFxActive(0) || proc.isFxActive(1)) parts.add("DLY");
    if (proc.isFxActive(2)) parts.add("REV");
    const auto fxText = parts.joinIntoString(" + ");
    const bool on = !parts.isEmpty();
    if (on != shownOn || flat != shownFlat || fxText != shownFx) { shownOn = on; shownFlat = flat; shownFx = fxText; repaint(); }
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
    const juce::String label = juce::String("EQ / FX   ") + (shownOn ? shownFx : juce::String("OFF"));
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
    for (auto* b : { &toneButton, &notesButton }) addAndMakeVisible(*b);
    toneButton.onClick = [this] { setParamValue(proc, "frankMode", 0.0f); };
    notesButton.onClick = [this] { setParamValue(proc, "frankMode", 1.0f); };
    toneButton.setTooltip("TONE: every note goes through all amps; each amp plays its part of the SOUND's spectrum");
    notesButton.setTooltip("NOTES: the guitar is split before the amps; each NOTE RANGE is played through its own amp "
                           "(low notes -> left section, high notes -> right section)");
}

juce::Rectangle<float> FrankensteinPanel::bandArea() const
{
    const auto m = mapArea();
    return { m.getX(), m.getBottom() + 4.0f, m.getWidth(), 12.0f };
}

juce::Rectangle<float> FrankensteinPanel::mapArea() const
{
    return getLocalBounds().toFloat().reduced(16.0f, 0.0f).withTrimmedTop(52.0f).withTrimmedBottom(46.0f);
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
    r.removeFromRight(20);
    widthSlider.setBounds(r.removeFromRight(170));
    r.removeFromRight(56);
    for (int i = 3; i >= 0; --i)
    {
        sectionButtons[(size_t) i].setBounds(r.removeFromRight(30));
        r.removeFromRight(4);
    }
    r.removeFromRight(76);
    notesButton.setBounds(r.removeFromRight(62));
    r.removeFromRight(4);
    toneButton.setBounds(r.removeFromRight(56));
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
    drawLabel(g, "SPLIT", { toneButton.getX() - 50, 12, 44, 26 }, textDim, juce::Justification::centredRight);
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

    // live note band: the spectrum of what is played (clean guitar), the played note marked
    {
        const auto b = bandArea();
        g.setColour(raised);
        g.fillRect(b);
        for (int i = 0; i < (int) spectrum.size(); ++i)
        {
            const float v = spectrum[(size_t) i];
            if (v <= 0.0f) continue;
            g.setColour(textDim.withAlpha(juce::jlimit(0.0f, 1.0f, v)));
            g.fillRect(b.getX() + (float) i, b.getY(), 1.0f, b.getHeight());
        }
        if (noteHz > 0.0f)
        {
            const float x = xForHz(noteHz);
            g.setColour(text);
            g.fillRect(x - 1.0f, b.getY() - 3.0f, 2.0f, b.getHeight() + 6.0f);
            g.setFont(Fonts::get().semibold(11.0f));
            const auto lr = juce::Rectangle<float>(x + 5.0f, b.getY() - 1.0f, 90.0f, b.getHeight() + 2.0f);
            g.setColour(background.withAlpha(0.85f));
            g.fillRect(lr.withWidth(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), noteName) + 6.0f));
            g.setColour(text);
            g.drawText(noteName, lr.translated(3.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }

    // frequency axis
    g.setFont(Fonts::get().regular(11.0f));
    g.setColour(textDim);
    const float axisY = bandArea().getBottom();
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xForHz(f);
        g.fillRect(x - 0.5f, axisY, 1.0f, 4.0f);
        g.drawText(f >= 1000 ? juce::String((int) (f / 1000)) + "k" : juce::String((int) f),
                   juce::Rectangle<float>(x - 20.0f, axisY + 5.0f, 40.0f, 13.0f), juce::Justification::centred, false);
    }

    const int hidden = juce::jlimit(2, 5, (int) std::lround(proc.params.getRawParameterValue("frankSections")->load())) - layout.numVisible;
    g.setFont(Fonts::get().regular(11.5f));
    g.setColour(textDim);
    const juce::String hint = layout.numVisible == 0 ? juce::String("All amps used here are muted or empty")
                            : hidden > 0 ? juce::String(hidden) + " section(s) hidden (muted or empty amp)"
                                         : juce::String("Drag a divider to move the hand-over. Click a section to choose its amp.");
    g.drawFittedText(hint, juce::Rectangle<float>(130.0f, 12.0f, (float) toneButton.getX() - 50.0f - 136.0f, 26.0f).toNearestInt(),
                     juce::Justification::centredLeft, 2, 0.9f);
}

void FrankensteinPanel::refresh()
{
    const auto l = proc.getFrankensteinLayout();
    const int K = juce::jlimit(2, 5, (int) std::lround(proc.params.getRawParameterValue("frankSections")->load()));
    for (int i = 0; i < 4; ++i)
        sectionButtons[(size_t) i].setToggleState(i + 2 == K, juce::dontSendNotification);
    // repaint the map only when the layout really changed (no constant redrawing / flicker)
    const bool changed = std::memcmp(&l.crossoverHz, &layout.crossoverHz, sizeof(l.crossoverHz)) != 0 || l.numVisible != layout.numVisible
                         || std::memcmp(&l.visibleDividersHz, &layout.visibleDividersHz, sizeof(l.visibleDividersHz)) != 0
                         || l.dividerIsMerged != layout.dividerIsMerged;
    bool slotsChanged = false;
    for (int i = 0; i < l.numVisible; ++i) slotsChanged = slotsChanged || l.visibleSections[(size_t) i].slot != layout.visibleSections[(size_t) i].slot;
    layout = l;
    const bool notes = paramValue(proc, "frankMode") > 0.5f;
    toneButton.setToggleState(!notes, juce::dontSendNotification);
    notesButton.setToggleState(notes, juce::dontSendNotification);
    if (changed || slotsChanged) repaint();

    // note band: spectrum of the clean guitar per pixel column, relative to its loudest part
    std::vector<float> mags;
    double binHz = 1.0;
    proc.getInputSpectrum(mags, binHz);
    const auto b = bandArea();
    spectrum.assign((size_t) b.getWidth(), 0.0f);
    float peak = -200.0f;
    for (auto v : mags) peak = std::max(peak, v);
    if (peak > -75.0f)
        for (int i = 0; i < (int) spectrum.size(); ++i)
        {
            const double f0 = hzForX(b.getX() + (float) i), f1 = hzForX(b.getX() + (float) i + 1.0f);
            const int k0 = juce::jlimit(1, (int) mags.size() - 1, (int) (f0 / binHz)), k1 = juce::jlimit(k0, (int) mags.size() - 1, (int) (f1 / binHz));
            float m = -200.0f;
            for (int k = k0; k <= k1; ++k) m = std::max(m, mags[(size_t) k]);
            spectrum[(size_t) i] = juce::jlimit(0.0f, 1.0f, (m - (peak - 45.0f)) / 45.0f); // the top 45 dB
        }
    const auto res = proc.analyseTuner();
    noteHz = res.valid && peak > -75.0f ? (float) res.frequencyHz : 0.0f;
    noteName = noteHz > 0.0f ? juce::String(res.noteName()) + juce::String(res.octave()) + "  " + juce::String(juce::roundToInt(noteHz)) + " Hz" : juce::String();
    repaint(bandArea().expanded(4.0f, 4.0f).withRight((float) getWidth()).toNearestInt());
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
    // Limits come only from the VISIBLE neighbouring dividers (1/6 octave apart). Boundaries of hidden
    // sections (muted / empty amps) never block the drag: they are pushed along, keeping their order.
    const int K = juce::jlimit(2, 5, (int) std::lround(proc.params.getRawParameterValue("frankSections")->load()));
    const double gap = std::pow(2.0, 1.0 / 6.0);
    double lo = 30.0, hi = 16000.0;
    if (dragDivider > 0) lo = layout.visibleDividersHz[(size_t) dragDivider - 1] * gap;
    if (dragDivider < layout.numVisible - 2) hi = layout.visibleDividersHz[(size_t) dragDivider + 1] / gap;
    const double hz = juce::jlimit(lo, juce::jmax(lo, hi), hzForX(e.position.x));
    auto div = [this](int k) { return (double) proc.params.getRawParameterValue("frankDiv" + juce::String(k + 1))->load(); };
    auto setDiv = [this](int k, double v) {
        if (auto* p = proc.params.getParameter("frankDiv" + juce::String(k + 1)))
            p->setValueNotifyingHost(p->convertTo0to1((float) juce::jlimit(30.0, 16000.0, v)));
    };
    setDiv(dragParam, hz);
    const double step = std::pow(2.0, 1.0 / 12.0);
    for (int k = dragParam + 1; k < K - 1; ++k) // hidden boundaries above: keep them above
        if (div(k) < div(k - 1) * step) setDiv(k, div(k - 1) * step);
    for (int k = dragParam - 1; k >= 0; --k)    // and below
        if (div(k) > div(k + 1) / step) setDiv(k, div(k + 1) / step);
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
    // input calibration: drive every capture at the level it was recorded at
    const bool cal = proc.isInputCalibrationOn();
    const double dbu = proc.getInterfaceInputDbu();
    m.addItem(10, "Calibrate input to each capture", true, cal);
    juce::PopupMenu levels;
    for (int i = 0; i <= 56; ++i)
    {
        const double v = i * 0.5; // 0 ... +28 dBu
        if (std::fmod(v, 1.0) != 0.0 && (v < 6.0 || v > 22.0)) continue;
        levels.addItem(100 + i, (v > 0 ? "+" : "") + juce::String(v, 1) + " dBu" + (i == 24 ? "  (NAM default)" : ""),
                       true, std::abs(v - dbu) < 0.01);
    }
    m.addSubMenu("My interface's input level: " + juce::String(dbu, 1) + " dBu", levels, true);
    m.addSeparator();
    const bool mc = proc.isMultiCoreOn();
    m.addItem(20, "Use several CPU cores" + (mc ? " (" + juce::String(proc.getNumWorkerThreads() + 1) + " in use)" : juce::String()), true, mc);
    m.addSeparator();
    m.addItem(2, "Open preset folder");
    m.addItem(3, "About AMPSURD");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settingsButton), [this, lm, cal, dbu](int r) {
        if (r == 1) setParamValue(proc, "levelMatch", lm ? 0.0f : 1.0f);
        if (r == 10) proc.setInputCalibration(!cal, dbu);
        if (r == 20) proc.setMultiCore(!proc.isMultiCoreOn());
        if (r >= 100 && r <= 156) proc.setInputCalibration(true, (r - 100) * 0.5);
        if (r == 2) { proc.getPresetFolder().createDirectory(); proc.getPresetFolder().revealToUser(); }
        if (r == 3)
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::NoIcon, "AMPSURD " JucePlugin_VersionString,
                "Five NAM captures, blended into one guitar sound.\n\n"
                "Free software under the GNU AGPLv3.\n"
                "Uses NeuralAmpModelerCore (MIT), JUCE (AGPLv3), the VST3 SDK (MIT), Eigen (MPL-2.0) "
                "and the Inter typeface (SIL OFL 1.1).\n\n"
                "Level match: every capture is measured by AMPSURD and played at the same perceived loudness.\n"
                "Calibrate input: captures that store their recording input level (dBu) are driven exactly as "
                "recorded - set your interface's input level (the dBu that gives 0 dBFS) once.\n"
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
