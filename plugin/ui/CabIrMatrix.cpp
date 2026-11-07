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
    for (auto* b : { &closeButton, &hoverButton, &snapButton, &saveButton, &exportButton, &resetButton })
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
    resetButton.onClick = [this] { resetRows(); };
    resetButton.setTooltip("All rows back in their order and not slid (the default map)");
    closeButton.setTooltip("Back to the IR editor (Esc)");
    hoverButton.setTooltip("On: moving the mouse over the map lets you hear each point. Leaving the map returns to the "
                           "point you clicked. Off: only clicking / dragging changes the IR");
    snapButton.setTooltip("On: only the centres of the boxes (one IR, not blended). Off: anything in between, too");
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
    if (chosenValid)
    {
        chosen = M::onDefaultMap(chosen); // a map of its own, or an old point on the default map
        order = chosen.order;
        offsets = chosen.offset;
    }
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
        b.removeFromLeft(8);
        resetButton.setBounds(b.removeFromLeft(110));
        exportButton.setBounds(b.removeFromRight(140));
        b.removeFromRight(8);
        saveButton.setBounds(b.removeFromRight(90));
    }
    r.removeFromBottom(10);
    labels = r.removeFromLeft(176);
    axis = r.removeFromTop(34);
    labels.removeFromTop(34);
    map = r.reduced(0, 2);
    labels = labels.withHeight(map.getHeight()).withY(map.getY());
}

CabPoint CabIrMatrix::onMap(CabPoint p) const
{
    if (p.hasLayout) return M::clamp(p);
    p = M::clamp(p);
    return M::atMap(order, offsets, (p.mm - M::kMinMm) / M::kStepMm, p.row);
}

juce::Point<float> CabIrMatrix::positionOf(CabPoint p) const
{
    // box centres: x = 0..15, y = 0..8 (box units); a simple point = where it sits on the default map
    p = p.hasLayout ? M::clamp(p) : M::onDefaultMap(p);
    const float cw = (float) map.getWidth() / (float) M::kColumns, rh = (float) map.getHeight() / (float) M::kRows;
    return { (float) map.getX() + cw * (0.5f + (float) p.x), (float) map.getY() + rh * (0.5f + (float) p.y) };
}

CabPoint CabIrMatrix::pointAt(juce::Point<int> pt) const
{
    const double cw = map.getWidth() / (double) M::kColumns, rh = map.getHeight() / (double) M::kRows;
    const double x = (pt.x - map.getX()) / cw - 0.5, y = (pt.y - map.getY()) / rh - 0.5;
    return snapped(M::atMap(order, offsets, x, y));
}

CabPoint CabIrMatrix::snapped(CabPoint p) const
{
    if (!snapButton.getToggleState()) return p;
    return M::atMap(p.order, p.offset, std::round(p.x), std::round(p.y));
}

int CabIrMatrix::rowAtY(int y) const
{
    const double rh = map.getHeight() / (double) M::kRows;
    return juce::jlimit(0, M::kRows - 1, (int) std::floor((y - map.getY()) / rh));
}

juce::Rectangle<int> CabIrMatrix::rowLabel(int d) const
{
    const float rh = (float) map.getHeight() / (float) M::kRows;
    return { labels.getX(), map.getY() + (int) std::round(rh * d), labels.getWidth() - 6, (int) std::round(rh) };
}

juce::Rectangle<int> CabIrMatrix::leftArrow(int d) const { auto r = rowLabel(d); return r.withX(r.getRight() - 40).withWidth(18).reduced(0, 2); }
juce::Rectangle<int> CabIrMatrix::rightArrow(int d) const { auto r = rowLabel(d); return r.withX(r.getRight() - 20).withWidth(18).reduced(0, 2); }

void CabIrMatrix::slideRow(int d, int boxes)
{
    if (d < 0 || d >= M::kRows || boxes == 0) return;
    offsets[(size_t) d] = (((offsets[(size_t) d] + boxes) % 30) + 30) % 30;
    layoutChanged();
}

void CabIrMatrix::moveRow(int from, int to)
{
    from = juce::jlimit(0, M::kRows - 1, from);
    to = juce::jlimit(0, M::kRows - 1, to);
    if (from == to) return;
    auto moveIn = [from, to](std::array<int, 9>& a) {
        const int v = a[(size_t) from];
        if (from < to) for (int i = from; i < to; ++i) a[(size_t) i] = a[(size_t) i + 1];
        else for (int i = from; i > to; --i) a[(size_t) i] = a[(size_t) i - 1];
        a[(size_t) to] = v;
    };
    moveIn(order);
    moveIn(offsets);
    layoutChanged();
}

void CabIrMatrix::resetRows()
{
    order = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    offsets = {};
    layoutChanged();
}

void CabIrMatrix::layoutChanged()
{
    // the points stay where they are on the screen; what lies under them now is heard
    if (hovering) { hover = M::atMap(order, offsets, hover.x, hover.y); request(hover); }
    if (chosenValid)
    {
        chosen = M::atMap(order, offsets, chosen.x, chosen.y);
        if (!hovering) request(chosen);
    }
    else hover = M::atMap(order, offsets, hover.x, hover.y);
    repaint();
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
    p = snapped(onMap(p));
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
    p = snapped(onMap(p));
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
    const bool onLabel = labels.contains(e.getPosition());
    setMouseCursor(onLabel ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    if (onLabel) repaint(labels);
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
    if (slot < 0) return;
    const auto pos = e.getPosition();
    if (labels.contains(pos))
    {
        const int d = rowAtY(pos.y);
        if (leftArrow(d).contains(pos)) { slideRow(d, -1); return; }
        if (rightArrow(d).contains(pos)) { slideRow(d, 1); return; }
        rowDrag = RowDrag::undecided;
        dragRow = d;
        dragStart = pos;
        dragStartOffset = offsets[(size_t) d];
        return;
    }
    if (!map.contains(pos)) return;
    // on the boxes: a click chooses the point; dragging sideways slides that row (whole boxes, owner 2026-11-01);
    // dragging up / down moves the chosen point (between the rows)
    mapDrag = RowDrag::undecided;
    dragRow = rowAtY(pos.y);
    dragStart = pos;
    dragStartOffset = offsets[(size_t) dragRow];
}

void CabIrMatrix::mouseDrag(const juce::MouseEvent& e)
{
    if (rowDrag != RowDrag::none)
    {
        const auto delta = e.getPosition() - dragStart;
        if (rowDrag == RowDrag::undecided && std::max(std::abs(delta.x), std::abs(delta.y)) > 5)
            rowDrag = std::abs(delta.x) >= std::abs(delta.y) ? RowDrag::slide : RowDrag::move;
        if (rowDrag == RowDrag::slide)
        {
            // the row follows the mouse in whole boxes, so the boxes stay below each other
            const double cw = map.getWidth() / (double) M::kColumns;
            const int want = (((dragStartOffset + (int) std::lround(delta.x / cw)) % 30) + 30) % 30;
            if (want != offsets[(size_t) dragRow]) { offsets[(size_t) dragRow] = want; layoutChanged(); }
        }
        else if (rowDrag == RowDrag::move)
        {
            const int to = rowAtY(e.getPosition().y);
            if (to != dragRow) { moveRow(dragRow, to); dragRow = to; }
        }
        return;
    }
    if (mapDrag != RowDrag::none)
    {
        const auto delta = e.getPosition() - dragStart;
        if (mapDrag == RowDrag::undecided && std::max(std::abs(delta.x), std::abs(delta.y)) > 5)
        {
            mapDrag = std::abs(delta.x) >= std::abs(delta.y) ? RowDrag::slide : RowDrag::move;
            if (mapDrag == RowDrag::move) { dragging = true; endPreview(); }
        }
        if (mapDrag == RowDrag::slide)
        {
            const double cw = map.getWidth() / (double) M::kColumns;
            const int want = (((dragStartOffset + (int) std::lround(delta.x / cw)) % 30) + 30) % 30;
            if (want != offsets[(size_t) dragRow]) { offsets[(size_t) dragRow] = want; layoutChanged(); }
        }
        else if (mapDrag == RowDrag::move) choose(pointAt(e.getPosition()));
        return;
    }
    if (dragging) choose(pointAt(e.getPosition()));
}

void CabIrMatrix::mouseUp(const juce::MouseEvent& e)
{
    if (mapDrag == RowDrag::undecided) choose(pointAt(e.getMouseDownPosition())); // a click on the boxes
    mapDrag = RowDrag::none;
    rowDrag = RowDrag::none;
    dragRow = -1;
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
    g.drawText("Calculated 4x12 cabinet IRs. A box's centre = that IR alone; anywhere else = the boxes around it blended.",
               header.withTrimmedRight(90), juce::Justification::centredLeft, true);

    const float cw = (float) map.getWidth() / (float) M::kColumns, rh = (float) map.getHeight() / (float) M::kRows;
    label(g, "MM FROM THE CENTRE (25 = CAP EDGE, 100 = CONE).   CLICK = CHOOSE.   DRAG A ROW SIDEWAYS = SLIDE IT.   DRAG A NAME UP / DOWN = MOVE THE ROW.",
          axis.withTrimmedTop(16), textDim, juce::Justification::centredLeft, 10.5f);
    label(g, "SPEAKER / MIC", labels.withY(axis.getY()).withHeight(30), textDim, juce::Justification::bottomLeft, 10.5f);

    for (int d = 0; d < M::kRows; ++d)
    {
        const int row = order[(size_t) d];
        const float y = (float) map.getY() + rh * d;
        // a line where the speaker changes
        if (d == 0 || M::speakerOfRow(order[(size_t) d - 1]) != M::speakerOfRow(row))
        {
            g.setColour(line);
            g.drawHorizontalLine((int) y, (float) labels.getX(), (float) map.getRight());
        }
        const auto lr = rowLabel(d);
        const bool grabbed = dragRow == d && (rowDrag != RowDrag::none || mapDrag == RowDrag::slide);
        if (grabbed || (isMouseOver(true) && lr.contains(getMouseXYRelative())))
        {
            g.setColour(text.withAlpha(0.06f));
            g.fillRect(lr);
        }
        label(g, M::speakerName(M::speakerOfRow(row)), lr.withWidth(68).withTrimmedLeft(4), text, juce::Justification::centredLeft, 12.0f); // "HEAVY 77" fits
        g.setColour(textDim);
        g.setFont(Fonts::get().regular(12.0f));
        g.drawText(M::micName(M::micOfRow(row)), lr.withTrimmedLeft(72).withTrimmedRight(42), juce::Justification::centredLeft, false);
        g.setFont(Fonts::get().regular(13.0f));
        g.setColour(textDim);
        g.drawText("<", leftArrow(d), juce::Justification::centred, false);
        g.drawText(">", rightArrow(d), juce::Justification::centred, false);
        // the boxes: the position number in the middle (= that IR alone); darker = further out on the cone
        for (int c = 0; c < M::kColumns; ++c)
        {
            const double mm = M::mmOnMap(offsets[(size_t) d], c);
            const auto box = juce::Rectangle<float>((float) map.getX() + cw * c, y, cw, rh).reduced(1.5f);
            // greyscale (owner 2026-11-01): light = bright (cap edge), dark = dark (cone), a clear step from box to box
            const float bright = (float) (1.0 - (mm - M::kMinMm) / (M::kMaxMm - M::kMinMm));
            g.setColour(juce::Colour::greyLevel(0.07f + 0.43f * bright));
            g.fillRoundedRectangle(box, 2.0f);
            g.setColour(juce::Colour::greyLevel(bright > 0.55f ? 0.05f : 0.92f).withAlpha(0.9f));
            g.setFont(Fonts::get().regular(juce::jmin(12.0f, rh * 0.45f)));
            g.drawText(juce::String((int) std::lround(mm)), box, juce::Justification::centred, false);
        }
    }
    g.setColour(line);
    g.drawHorizontalLine(map.getBottom(), (float) labels.getX(), (float) map.getRight());
    g.drawRect(map);

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
        g.drawEllipse(c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f, 2.5f);
    }

    // what is heard, and messages
    auto info = bottom.withTrimmedLeft(415).withTrimmedRight(250);
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
