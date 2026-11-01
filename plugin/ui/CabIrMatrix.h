#pragma once

// AMPSURD IRs (2026-10-28): the owner's synthetic 4x12 cabinet IRs as one map to play with.
//
//   rows    = speaker x microphone (V30 / DV77 / SWAMP x SM57 / MD421 / R121)
//   boxes   = mic position, 25 mm (cap edge) .. 100 mm (on the cone), every 5 mm; the number is in the box
//
// Rows can be slid left / right by whole boxes (drag a row's name sideways, or its < > arrows) and moved up /
// down (drag the name up / down), owner 2026-11-01. A row runs there and back (25 .. 100 .. 25 ..), so it can be
// slid endlessly. A box's centre = that one IR; anywhere else = the boxes around it blended (left / right = the
// positions in between, up / down = the two rows, each at its own position). RESET ROWS = the default map.
//
// Click or drag anywhere: the IR follows the mouse - between the dots AMPSURD calculates the in-between IR
// (core/CabIrModel), so moving across the map is like moving the mic / swapping the mic or speaker. HOVER TO
// LISTEN: just moving the mouse over the map lets you hear the points; leaving the map returns to the chosen
// one (and hovering never becomes an undo step). SNAP TO GRID: only the 144 set points. SAVE IR writes the
// chosen IR as a WAV (named like the set's files), EXPORT ALL 144 the whole set at the session's rate.
// Shown over the slots and the centre area (an overlay inside the plugin window, no extra OS window).

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

namespace ampsurd::ui
{
class CabIrMatrix final : public juce::Component, private juce::Timer
{
public:
    explicit CabIrMatrix(AmpsurdProcessor&);
    ~CabIrMatrix() override;
    void open(int slot);  // shows it for this amp
    void close();         // back to what was chosen (a hover preview ends)
    int getSlot() const { return slot; }
    std::function<void()> onClose;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // tests: the point under a position of the map, and choosing / previewing it as the mouse would
    juce::Rectangle<int> getMapArea() const { return map; }
    CabPoint pointAt(juce::Point<int> p) const;
    juce::Point<float> positionOf(CabPoint p) const;
    void choose(CabPoint p);
    void preview(CabPoint p);
    void endPreview();
    bool hasChosen() const { return chosenValid; }
    CabPoint getChosen() const { return chosen; }
    void setHoverToListen(bool on) { hoverButton.setToggleState(on, juce::dontSendNotification); }
    void setSnap(bool on) { snapButton.setToggleState(on, juce::dontSendNotification); }
    // tests / the row handles: slide display row d by `boxes`, move display row `from` to `to`, default map
    void slideRow(int d, int boxes);
    void moveRow(int from, int to);
    void resetRows();
    const std::array<int, 9>& getOrder() const { return order; }
    const std::array<int, 9>& getOffsets() const { return offsets; }
    void flush() { timerCallback(); } // tests: send the pending IR now
    // the WAV of a point, levelled like the original set (all 144 equally loud, peak of the loudest -1 dBFS)
    static bool writeIrFile(CabPoint p, double rate, const juce::File& f, juce::String& error);

private:
    void timerCallback() override;
    void request(CabPoint p);
    void saveChosen();
    void exportAll();
    void setStatus(const juce::String& s) { status = s; repaint(); }
    CabPoint snapped(CabPoint p) const;
    CabPoint onMap(CabPoint p) const;      // a simple point -> the current map at the same place
    void layoutChanged();                 // the chosen / hovered point stays where it is: its sound follows the rows
    int rowAtY(int y) const;
    juce::Rectangle<int> rowLabel(int d) const, leftArrow(int d) const, rightArrow(int d) const;

    AmpsurdProcessor& proc;
    int slot = -1;
    juce::Rectangle<int> map, labels, axis, bottom;
    juce::TextButton closeButton { "CLOSE" }, hoverButton { "HOVER TO LISTEN" }, snapButton { "SNAP TO GRID" },
                     saveButton { "SAVE IR" }, exportButton { "EXPORT ALL 144" }, resetButton { "RESET ROWS" };
    std::array<int, 9> order { 0, 1, 2, 3, 4, 5, 6, 7, 8 }, offsets {};
    enum class RowDrag { none, undecided, slide, move } rowDrag = RowDrag::none, mapDrag = RowDrag::none;
    int dragRow = -1, dragStartOffset = 0;
    juce::Point<int> dragStart;
    CabPoint chosen, hover, pending, sent;
    bool chosenValid = false, hovering = false, dragging = false, hasPending = false;
    AmpsurdProcessor::IrStatus original; // the IR before the map was opened (restored after a hover preview)
    juce::String status;
};
} // namespace ampsurd::ui
