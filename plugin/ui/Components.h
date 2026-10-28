#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "../player/VideoCapture.h"
#include "CameraWindow.h"
#include "Theme.h"

namespace ampsurd::ui
{

// ---------------------------------------------------------------------------------------------
// Filename typography: ONE fixed font size for every slot; long names wrap over several
// centred lines at natural break points (after _ - . and spaces); characters are never changed,
// no hyphens are inserted. If it still does not fit, the last line is truncated with "...".
struct FilenameLayout
{
    static constexpr float kFontSize = 21.0f;
    static constexpr float kLineHeight = 27.0f;
    static constexpr int kMaxLines = 4;

    static juce::StringArray wrap(const juce::String& text, const juce::Font& font, float maxWidth, int maxLines,
                                  bool& truncated);
    static void draw(juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, juce::Colour colour);
};

// ---------------------------------------------------------------------------------------------
// Vertical mixer-style fader showing the slot's share of the blend. Linked: moving it rescales
// the other loaded slots so the stored mix always totals 100 %.
class MixFader final : public juce::Component
{
public:
    MixFader(AmpsurdProcessor&, int slot);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    float valueFromY(float y) const;
    juce::Rectangle<float> trackArea() const;

private:
    AmpsurdProcessor& proc;
    int slot;
    bool dragging = false;
};

// ---------------------------------------------------------------------------------------------
class SlotComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            public juce::DragAndDropTarget,
                            public juce::SettableTooltipClient
{
public:
    SlotComponent(AmpsurdProcessor&, int slot);
    ~SlotComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped(const juce::StringArray&, int, int) override;

    // drag an amp onto another slot (grab it by its name / number): the two slots swap
    void mouseDrag(const juce::MouseEvent&) override;
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override { dragHover = true; repaint(); }
    void itemDragExit(const SourceDetails&) override { dragHover = false; repaint(); }
    void itemDropped(const SourceDetails&) override;

    void refresh(bool isSelected, bool irEditorOpen = false);
    bool isSelected() const { return selected; } // the editor fills a selected slot (under the dragon)
    std::function<void(int)> onEditClicked, onIrClicked;

private:
    void chooseFile();
    juce::Rectangle<int> nameArea() const;

    AmpsurdProcessor& proc;
    const int slot;
    juce::TextButton loadButton { "LOAD NAM" }, editButton { "EDIT" }, irButton { "ADD IR" },
                     soloButton { "SOLO" }, muteButton { "MUTE" }, removeButton { "REMOVE" }, ecoButton { "ECO" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ecoAtt;
    double removeArmedAt = -1.0; // REMOVE needs a second click within 3 s
    AmpsurdProcessor::IrStatus irStatus;
    bool irOn = true;
    MixFader fader;
    juce::Slider panSlider;   // per-amp PAN (stereo width)
    juce::Label panValue;     // C / L35 / R20 - click and type a value
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAtt, muteAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panAtt;
    juce::Rectangle<int> panArea() const;
    void updatePanText();
    juce::Rectangle<int> irLineArea() const;
    juce::String panText;
    std::unique_ptr<juce::FileChooser> chooser;

    AmpsurdProcessor::SlotStatus status;
    float effective = 0.0f;
    bool audible = false, selected = false, dragHover = false;
    juce::String lastTooltip;
};

// ---------------------------------------------------------------------------------------------
// COPY / CANCEL COPY / PASTE / SURE? button of an EQ (amp EQ, IR EQ, Global EQ). The clipboard lives in the
// processor (session only), so every EQ's button knows what is copied. PASTE needs a second click
// within 3 s (SURE?); a click anywhere else cancels SURE?.
class EqCopyButton final : public juce::TextButton, private juce::Timer
{
public:
    explicit EqCopyButton(AmpsurdProcessor&);
    ~EqCopyButton() override;
    void setTarget(int t) { target = t; armedAt = -1.0; update(); }
    void clicked() override;
    void update(); // also from tests

private:
    void timerCallback() override { update(); }
    struct ClickWatcher final : juce::MouseListener // a click anywhere else cancels SURE?
    {
        explicit ClickWatcher(EqCopyButton& b) : owner(b) {}
        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.eventComponent != &owner && owner.armedAt > 0.0) { owner.armedAt = -1.0; owner.update(); }
        }
        EqCopyButton& owner;
    };
    AmpsurdProcessor& proc;
    int target = -1;
    double armedAt = -1.0;
    ClickWatcher watcher { *this };
};

// ---------------------------------------------------------------------------------------------
// 10-band parametric EQ graph, monochrome. Drag a node: horizontal = frequency, vertical = gain.
// Mouse wheel = Q. Double-click a node = reset that band.
class EqGraph final : public juce::Component
{
public:
    explicit EqGraph(AmpsurdProcessor&);
    void setSlot(int s);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // repaint only when something visible changed
    void refreshIfChanged();

private:
    juce::Rectangle<float> plotArea() const;
    float xForFreq(float f) const;
    float freqForX(float x) const;
    float yForGain(float g) const;
    float gainForY(float y) const;
    static constexpr float kGrabRadius = 14.0f;
    // nearest point within maxDistance (pixels); maxDistance < 0 = nearest point anywhere
    int bandAt(juce::Point<float>, float maxDistance) const;
    juce::Point<float> nodePos(int band, const std::array<ampsurd::EqBand, ampsurd::ParametricEq::kNumBands>&) const;
    std::array<ampsurd::EqBand, ampsurd::ParametricEq::kNumBands> bands() const;
    juce::RangedAudioParameter* bandParam(int band, const char* name) const;

    AmpsurdProcessor& proc;
    int slot = -1;
    int hoverBand = -1, activeBand = -1;
    std::array<float, 3 * ampsurd::ParametricEq::kNumBands + 2> lastSeen {};
    std::array<float, 3 * ampsurd::ParametricEq::kNumBands + 1> lastGlobal {};
    juce::ComboBox bandsBox; // how many bands this EQ shows (2 ... 10)
    juce::TextButton presetsButton { "EQ PRESETS" }; // factory curves, the user's own, save as (owner 2026-10-28)
    void showPresetMenu();
    void saveAsDialog();
};

// ---------------------------------------------------------------------------------------------
class AlignPanel final : public juce::Component
{
public:
    explicit AlignPanel(AmpsurdProcessor&);
    void setSlot(int s);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    AmpsurdProcessor& proc;
    int slot = -1;
    juce::TextButton autoButton { "AUTO" }, freeButton { "FREE" }, resetButton { "RESET" };
    juce::Slider timeSlider, phaseSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAtt, phaseAtt;
    juce::String line1, line2;
};

// ---------------------------------------------------------------------------------------------
// Per-amp INPUT (drive, loudness kept automatically) and OUTPUT (level in the mix).
class LevelPanel final : public juce::Component
{
public:
    explicit LevelPanel(AmpsurdProcessor&);
    void setSlot(int s);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    AmpsurdProcessor& proc;
    int slot = -1;
    juce::Slider inputSlider, outputSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAtt, outputAtt;
    juce::String info;
};

// ---------------------------------------------------------------------------------------------
class EditPanel final : public juce::Component
{
public:
    explicit EditPanel(AmpsurdProcessor&);
    void setSlot(int s);
    int getSlot() const { return slot; }
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    std::function<void()> onClose;

private:
    AmpsurdProcessor& proc;
    int slot = -1;
    EqGraph graph;
    AlignPanel align;
    LevelPanel level;
    juce::TextButton eqOnButton { "EQ ON" }, flatButton { "FLAT" }, removeButton { "REMOVE" }, closeButton { "CLOSE" };
    EqCopyButton copyButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqOnAtt;
    juce::String title;
};

// ---------------------------------------------------------------------------------------------
// Global EQ: same EQ as the amps, applied to the complete blend. Replaces the gate + tuner while open.
// Effects after the gate: selector (DELAY 1 / DELAY 2 / REVERB / FLANGER, lit while on) and the
// controls of the selected effect. Every value can be typed (click it).
class FxSection final : public juce::Component
{
public:
    explicit FxSection(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    void select(int which);
    int getSelected() const { return selected; }

private:
    struct Row
    {
        juce::String label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    Row makeRow(const juce::String& label, const juce::String& paramId);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachButton(juce::Button&, const juce::String& id);
    void tap();

    AmpsurdProcessor& proc;
    int selected = 0;
    std::array<juce::TextButton, 5> selector;
    // delay 1 / 2
    std::array<juce::TextButton, 2> dOn, dSync, dPing;
    std::array<juce::ComboBox, 2> dNote;
    std::array<std::array<Row, 4>, 2> dRows;   // TIME, FEEDBACK, LEVEL, TONE
    Row tempoRow;
    juce::TextButton tapButton { "TAP" };
    std::vector<double> taps;
    // reverb
    juce::TextButton rOn { "ON" };
    std::array<juce::TextButton, 5> rType;
    std::array<Row, 4> rRows;                   // DECAY, PRE-DELAY, TONE, LEVEL
    // flanger
    juce::TextButton fOn { "ON" };
    std::array<Row, 4> fRows;                   // RATE, DEPTH, FEEDBACK, MIX
    // pitch (tunes the guitar before the amps)
    juce::TextButton pOn { "ON" };
    std::array<Row, 2> pRows;                   // SEMITONES, CENTS
    std::array<juce::TextButton, 4> pPreset;    // quick tunings
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, 2> noteAtts;
    juce::String syncInfo;
};

// Chromatic tuner on the clean DI - the left column of the GLOBAL section.
class TunerPanel final : public juce::Component
{
public:
    explicit TunerPanel(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    AmpsurdProcessor& proc;
    juce::TextButton muteButton { "MUTE OUTPUT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt;
    ampsurd::PitchDetector::Result shown;
    std::array<double, 5> recentCents {};
    int recentCount = 0, lastNote = -1;
    double lastValidMs = 0.0;
};

class GlobalEqPanel final : public juce::Component
{
public:
    explicit GlobalEqPanel(AmpsurdProcessor&);
    void selectFx(int which) { fx.select(which); }
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    std::function<void()> onClose;

private:
    AmpsurdProcessor& proc;
    TunerPanel tuner;
    EqGraph graph;
    FxSection fx;
    juce::TextButton eqOnButton { "EQ ON" }, flatButton { "FLAT" }, closeButton { "CLOSE" };
    EqCopyButton copyButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqOnAtt;

public:
    // CLOSE only when there is something to return to (Frankenstein)
    void setCloseVisible(bool v) { closeButton.setVisible(v); }
};

// IR editor for one slot: left = the cabinet IR (load / replace, IR ON, remove, file info),
// right = the IR's own 10-band EQ (same EQ as everywhere else, drawn a bit smaller).
class IrPanel final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit IrPanel(AmpsurdProcessor&);
    void setSlot(int s);
    int getSlot() const { return slot; }
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    std::function<void()> onClose;
    std::function<void(int)> onAmpsurdIrs; // AMPSURD IRs: the editor shows the IR map for this amp

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    void chooseFile();
    AmpsurdProcessor& proc;
    int slot = -1;
    EqGraph graph;
    juce::TextButton ampsurdIrsButton { "AMPSURD IRs" }, loadButton { "LOAD IR" }, irOnButton { "IR ON" }, removeButton { "REMOVE IR" },
                     eqOnButton { "EQ ON" }, flatButton { "FLAT" }, closeButton { "CLOSE" };
    EqCopyButton copyButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> irOnAtt, eqOnAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    AmpsurdProcessor::IrStatus shown;
    juce::String namName;
};

// Vertical GLOBAL EQ button at the right of the centre area; lit while the Global EQ is on.
class GlobalEqButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GlobalEqButton(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void refresh();
    std::function<void()> onClick;

private:
    AmpsurdProcessor& proc;
    bool shownOn = false, shownFlat = true;
    juce::String shownFx;
};

// ---------------------------------------------------------------------------------------------
// Noise gate (NS-2 style) - shown in the centre area while no amp is being edited.
class GatePanel final : public juce::Component
{
public:
    explicit GatePanel(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void refresh();

private:
    juce::Rectangle<float> meterArea() const;
    float xForDb(float db) const;
    float dbForX(float x) const;

    AmpsurdProcessor& proc;
    juce::Slider thresholdSlider, decaySlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thrAtt, decAtt;
    float level = 0.0f;
    float gain = 1.0f;
    bool draggingThreshold = false;
};

// "Create Frankenstein": the spectrum as a map of sections, each played by one amp.
class FrankensteinPanel final : public juce::Component
{
public:
    explicit FrankensteinPanel(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void refresh();

private:
    int availableAmps = 0; // loaded + unmuted amps (limits the SECTIONS buttons)
    juce::Rectangle<float> mapArea() const;
    juce::Rectangle<float> bandArea() const;     // live note band under the map
    float xForHz(double hz) const;
    double hzForX(float x) const;
    int dividerAt(juce::Point<float>) const;     // visible divider index or -1
    void chooseAmpForSection(int visibleIndex);

    AmpsurdProcessor& proc;
    std::array<juce::TextButton, 4> sectionButtons { juce::TextButton { "2" }, juce::TextButton { "3" },
                                                     juce::TextButton { "4" }, juce::TextButton { "5" } };
    juce::TextButton exitButton { "EXIT" }, toneButton { "TONE" }, notesButton { "NOTES" };
    juce::Slider widthSlider;
    std::vector<float> spectrum;  // dB per column of the note band
    float noteHz = 0.0f;
    juce::String noteName;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAtt;
    ampsurd::FrankensteinLayout layout;
    int dragDivider = -1, dragParam = -1, hoverDivider = -1;
};



// ---------------------------------------------------------------------------------------------
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setPeak(float linear);
    void paint(juce::Graphics&) override;

private:
    float level = 0.0f, hold = 0.0f;
    int holdCount = 0;
};

// Master section (bottom): NOISE GATE strip on top (always visible), then INPUT / OUTPUT / LIMIT and the
// FRANKENSTEIN / ROOT / GATE / BYPASS buttons.
class MasterPanel final : public juce::Component
{
public:
    static constexpr int kGateRowH = 40;
    explicit MasterPanel(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    AmpsurdProcessor& proc;
    juce::Slider inputSlider, outputSlider;
    juce::TextButton bypassButton { "BYPASS" }, gateButton { "GATE" }, frankButton { "FRANKENSTEIN" };
    juce::TextButton muteButton { "MUTE" }; // the whole output to silence; red when on
    juce::TextButton rootButton { "ROOT" }; // experimental module (only with AMPSURD_ROOT)
    juce::TextButton doublerButton { "DOUBLER" }; // experimental module (only with AMPSURD_DOUBLER)

public:
    std::function<void()> onRootClick; // the editor opens / closes the ROOT panel
    std::function<void()> onDoublerClick; // ... the DOUBLER panel
    std::function<void()> onFrankClick;   // the editor shows / switches FRANKENSTEIN (2026-10-28: a click while another view
                                          // was open only toggled it invisibly - a tester pressed it ~5x)

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateAtt;
    LevelMeter inMeter, outMeterL, outMeterR; // output: left and right channel
    GatePanel gate;
    juce::Rectangle<int> controlsRow() const { return getLocalBounds().withTrimmedTop(kGateRowH); }
    // shown when only the OTHER interface input has a signal (guitar plugged into input 2, e.g.
    // the instrument jack of a Scarlett Solo): one click switches the guitar input
    juce::TextButton inputHint;
    int otherInputOnly = 0; // refresh ticks (30 Hz) with signal only on the other input
    int shownGuitarInput = -1;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inAtt, outAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    float limitDb = 0.0f;
    int limitHold = 0;
};

// ---------------------------------------------------------------------------------------------
// Undo / redo button: the usual curved arrow instead of text (same frame as the other buttons).
class ArrowButton final : public juce::TextButton
{
public:
    explicit ArrowButton(bool redoArrow) : redo(redoArrow) {}
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    bool redo;
};

class HeaderBar final : public juce::Component
{
public:
    explicit HeaderBar(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

    // SAVE AS writes what the chosen file type says: .ampsurd = the preset, .ampsurdpack = a pack (preset +
    // the NAM / IR files it uses + README, the same as PRESET -> Export pack). Public for the tests.
    bool saveAsFile(const juce::File& f, juce::String& error);
    static constexpr const char* kSaveAsPatterns = "*.ampsurd;*.ampsurdpack";

private:
    void showPresetMenu();
    void save(bool saveAs);
    void showSettings();

    void exportPack();
    void importPack();

    AmpsurdProcessor& proc;
    juce::TextButton presetButton, saveButton { "SAVE" }, saveAsButton { "SAVE AS" }, settingsButton;
    ArrowButton undoButton { false }, redoButton { true };
    juce::TextButton aButton { "A" }, bButton { "B" }, copyAbButton;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String presetName, status;
};

// ---------------------------------------------------------------------------------------------
// Player / recorder (standalone app only): backing track, guitar take, transport, save, bounce.
// Timeline of the player / recorder: backing track waveform above the guitar track waveform, a ruler,
// the playhead and the clips (recorded corrections) with draggable start / end edges.
// Click = go there, mouse wheel = zoom around the mouse, Shift + wheel (or horizontal) = scroll.
// A square with an X: deletes the track next to it (asks first).
class DeleteBox final : public juce::Button
{
public:
    DeleteBox() : juce::Button("delete") {}
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

class Timeline final : public juce::Component, private juce::ScrollBar::Listener
{
public:
    explicit Timeline(PlayerRecorder&);
    // the X before each track: the panel deletes (backing: removeBacking, guitar: clearTake - UNDO brings it back)
    std::function<void()> onDeleteBacking, onDeleteGuitar;
    void refreshDeleteBoxes(bool recording); // ~30 Hz from the panel: enabled when there is something to delete
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void zoom(double factor, double aroundSeconds);
    void zoomIn() { zoom(0.5, centreSeconds()); }
    void zoomOut() { zoom(2.0, centreSeconds()); }
    void fit();
    void followPlayhead();

private:
    struct EdgeHit { int clip = -1; bool start = true; };
    EdgeHit hitEdge(juce::Point<int>) const;
    double xToTime(float x) const { return viewStart + (double) (x - (float) laneArea().getX()) * secondsPerPixel; }
    float timeToX(double t) const { return (float) laneArea().getX() + (float) ((t - viewStart) / secondsPerPixel); }
    double centreSeconds() const { return viewStart + 0.5 * secondsPerPixel * laneArea().getWidth(); }
    static constexpr int kBoxW = 22;   // the delete box right before each track
    static constexpr int kLabelW = 100; // left column: track names, the GUITAR track's PAN, the delete boxes next to the tracks
    void askDelete(int which);         // 0 backing, 1 guitar: shows DELETE / CANCEL over that track
    juce::Rectangle<int> laneArea() const { return getLocalBounds().withTrimmedLeft(kLabelW).withTrimmedBottom(kScrollH + 4); }
    static constexpr int kScrollH = 8; // half of the first version (owner, 2026-10-22)
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;
    void updateScrollBar();
    juce::Rectangle<int> rulerArea() const { return laneArea().removeFromTop(16); }
    juce::Rectangle<int> backingLane() const;
    juce::Rectangle<int> guitarLane() const;
    void clampView();

    PlayerRecorder& player;
    juce::ScrollBar scroll { false }; // wide horizontal bar under the guitar track: drag to move along the song
    juce::Slider guitarPan;           // PAN of the GUITAR track (under its name)
    DeleteBox delBacking, delGuitar;
    juce::TextButton confirmYes { "DELETE" }, confirmNo { "CANCEL" };
    int confirming = -1;              // which track asks "delete?" (-1 none)
    double viewStart = 0.0, secondsPerPixel = 0.02;
    bool fitted = false;
    EdgeHit hover, drag;
    double dragTime = 0.0;
};

class PlayerPanel final : public juce::Component
{
public:
    PlayerPanel(AmpsurdProcessor&, PlayerRecorder&);
    ~PlayerPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    void recordPressed(bool fromCameraWindow = false); // REC (player, camera window) and the R key: audio take / punch, or a VIDEO take (VIDEO on)
    std::function<void()> onClose;

private:
    void loadBacking();
    void saveAs();
    void saveVideo();
    void setVideo(bool on);   // VIDEO on: camera window + camera; off: both closed
    void finishVideoTake();   // after a video take stopped: the camera file joins the take
    void startVideoTake();    // camera file + recording from STOP
    void setStatus(const juce::String& s);

    AmpsurdProcessor& proc;
    PlayerRecorder& player;
    Timeline timeline;
    juce::TextButton startButton { "|<" }, playButton { "PLAY" }, pauseButton { "PAUSE" }, stopButton { "STOP" }, recButton { "REC" },
                     zoomOutButton { "-" }, zoomInButton { "+" }, fitButton { "FIT" },
                     loadButton { "LOAD BACKING" }, removeButton { "REMOVE" }, saveButton { "SAVE AS..." }, bounceButton { "BOUNCE" },
                     closeButton { "CLOSE" };
    juce::Slider backVol, takeVol, offset;
    juce::ComboBox formatBox, rateBox, contentBox;
    // metronome row: CLICK, tempo (shared with delay SYNC), TAP, time signature, volume, count-in
    void applyMetronome();
    juce::TextButton clickButton { "CLICK" }, tapButton { "TAP" };
    juce::Slider bpmSlider, clickVol;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt;
    juce::ComboBox meterBox, countInBox, soundBox;
    std::vector<double> taps;
    juce::Rectangle<int> beatArea;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    float backLevel = 0.0f, takeLevel = 0.0f;
    juce::Rectangle<int> backMeter, takeMeter, statusArea, infoArea;
    // VIDEO takes (camera in its own window)
    juce::TextButton videoButton { "VIDEO" };
    VideoCapture camera;
    std::unique_ptr<CameraWindow> cameraWindow;
    bool videoRecording = false, videoSaving = false;
    double replaceArmedAt = -1.0;
    juce::File lastSavedVideo;
    juce::File savedTakeVideo; // the take's camera file whose MP4 has been saved: a new video take needs no REPLACE?
};

// ---------------------------------------------------------------------------------------------
// The five-headed AMPSURD dragon drawn faintly behind the UI (concept: project doc DRAGON_BACKGROUND).
// Body layer + one head per slot: asleep (empty slot), awake (capture loaded), red eye (SOLO / EDIT).
// Composed once into a cached image; redrawn only when a head changes. Art: assets/dragon (2x).
class DragonLayer
{
public:
    enum HeadState { asleep = 0, awake = 1, redEye = 2 };
    DragonLayer();
    // true when something changed (the caller repaints)
    bool setStates(const std::array<int, 5>& states);
    void draw(juce::Graphics&, juce::Rectangle<float> area) const;     // lines (under everything)
    void drawEyes(juce::Graphics&, juce::Rectangle<float> area) const; // red eyes (SOLO / EDIT)
    static constexpr float kStrength = 0.095f; // white lines at ~10 % = the light grey ink of the concept

private:
    void rebuild();
    juce::Image body;
    std::array<std::array<juce::Image, 2>, 5> heads;
    juce::Image cache;
    std::array<int, 5> states { -1, -1, -1, -1, -1 };
};

// ---------------------------------------------------------------------------------------------
// "BROUGHT TO YOU BY" + three equal logo areas. Logos are read from the embedded assets
// plugin/assets/logos/logo_1|2|3.(svg|png); until they exist, clean placeholders are drawn.
class BrandingFooter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    BrandingFooter();
    void paint(juce::Graphics&) override;
    void resized() override;
    void setCpuText(const juce::String& t) { if (t != cpuText) { cpuText = t; repaint(); } }

    static constexpr const char* kShopUrl = "https://www.ampsurd.com/";

private:
    juce::Rectangle<int> logoArea() const; // "BROUGHT TO YOU BY" + the three logos
    std::array<std::unique_ptr<juce::Drawable>, 3> logos;
    juce::TextButton shopButton { "BUY AMPSURD AMP PACKS" };
    juce::String cpuText;
};

} // namespace ampsurd::ui
