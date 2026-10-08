#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
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

    void refresh(bool isSelected, bool irEditorOpen = false);
    std::function<void(int)> onEditClicked, onIrClicked;

private:
    void chooseFile();
    juce::Rectangle<int> nameArea() const;

    AmpsurdProcessor& proc;
    const int slot;
    juce::TextButton loadButton { "LOAD NAM" }, editButton { "EDIT" }, irButton { "ADD IR" },
                     soloButton { "SOLO" }, muteButton { "MUTE" }, removeButton { "REMOVE" };
    double removeArmedAt = -1.0; // REMOVE needs a second click within 3 s
    AmpsurdProcessor::IrStatus irStatus;
    bool irOn = true;
    MixFader fader;
    juce::Slider panSlider;   // per-amp PAN (stereo width)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAtt, muteAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panAtt;
    juce::Rectangle<int> panArea() const;
    juce::Rectangle<int> irLineArea() const;
    juce::String panText;
    std::unique_ptr<juce::FileChooser> chooser;

    AmpsurdProcessor::SlotStatus status;
    float effective = 0.0f;
    bool audible = false, selected = false, dragHover = false;
    juce::String lastTooltip;
};

// ---------------------------------------------------------------------------------------------
// 10-band parametric EQ graph, monochrome. Drag a node: horizontal = frequency, vertical = gain.
// Mouse wheel = Q. Double-click a node = reset that band.
class EqGraph final : public juce::Component
{
public:
    explicit EqGraph(AmpsurdProcessor&);
    void setSlot(int s) { slot = s; activeBand = -1; repaint(); }
    void paint(juce::Graphics&) override;
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
    std::array<float, 3 * ampsurd::ParametricEq::kNumBands + 1> lastSeen {}, lastGlobal {};
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
    juce::TextButton eqOnButton { "EQ ON" }, flatButton { "FLAT" }, removeButton { "REMOVE" }, closeButton { "CLOSE" };
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
    std::array<juce::TextButton, 4> selector;
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
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, 2> noteAtts;
    juce::String syncInfo;
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
    EqGraph graph;
    FxSection fx;
    juce::TextButton eqOnButton { "EQ ON" }, flatButton { "FLAT" }, closeButton { "CLOSE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> eqOnAtt;
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

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    void chooseFile();
    AmpsurdProcessor& proc;
    int slot = -1;
    EqGraph graph;
    juce::TextButton loadButton { "LOAD IR" }, irOnButton { "IR ON" }, removeButton { "REMOVE IR" },
                     eqOnButton { "EQ ON" }, flatButton { "FLAT" }, closeButton { "CLOSE" };
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
    juce::TextButton onButton { "ON" };
    juce::Slider thresholdSlider, decaySlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thrAtt, decAtt;
    float level = 0.0f;
    float gain = 1.0f;
    bool draggingThreshold = false;
};

// Chromatic tuner on the clean DI - shown in the centre area while no amp is being edited.
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

class CentrePanel final : public juce::Component
{
public:
    explicit CentrePanel(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    GatePanel gatePanel;
    TunerPanel tunerPanel;
};

// ---------------------------------------------------------------------------------------------
class LevelMeter final : public juce::Component
{
public:
    void setPeak(float linear);
    void paint(juce::Graphics&) override;

private:
    float level = 0.0f, hold = 0.0f;
    int holdCount = 0;
};

class MasterPanel final : public juce::Component
{
public:
    explicit MasterPanel(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    AmpsurdProcessor& proc;
    juce::Slider inputSlider, outputSlider;
    juce::TextButton bypassButton { "BYPASS" }, gateButton { "GATE" }, frankButton { "CREATE FRANKENSTEIN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateAtt;
    LevelMeter inMeter, outMeter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inAtt, outAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    float limitDb = 0.0f;
    int limitHold = 0;
};

// ---------------------------------------------------------------------------------------------
class HeaderBar final : public juce::Component
{
public:
    explicit HeaderBar(AmpsurdProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    void showPresetMenu();
    void save(bool saveAs);
    void showSettings();

    AmpsurdProcessor& proc;
    juce::TextButton presetButton, saveButton { "SAVE" }, saveAsButton { "SAVE AS" }, settingsButton;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String presetName;
};

// ---------------------------------------------------------------------------------------------
// Player / recorder (standalone app only): backing track, guitar take, transport, save, bounce.
class PlayerPanel final : public juce::Component
{
public:
    PlayerPanel(AmpsurdProcessor&, PlayerRecorder&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    std::function<void()> onClose;

private:
    void loadBacking();
    void saveAs();
    void setStatus(const juce::String& s) { status = s; repaint(); }
    juce::Rectangle<int> column(int i) const;

    AmpsurdProcessor& proc;
    PlayerRecorder& player;
    juce::TextButton startButton { "|<" }, playButton { "PLAY" }, pauseButton { "PAUSE" }, stopButton { "STOP" }, recButton { "REC" },
                     loadButton { "LOAD BACKING" }, removeButton { "REMOVE" }, saveButton { "SAVE AS..." }, bounceButton { "BOUNCE" },
                     closeButton { "CLOSE" };
    juce::Slider backVol, takeVol, offset;
    juce::ComboBox formatBox, rateBox, contentBox;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    double recArmedAt = -1.0;
    float backLevel = 0.0f, takeLevel = 0.0f;
    juce::String shownTime;
};

// ---------------------------------------------------------------------------------------------
// "BROUGHT TO YOU BY" + three equal logo areas. Logos are read from the embedded assets
// plugin/assets/logos/logo_1|2|3.(svg|png); until they exist, clean placeholders are drawn.
class BrandingFooter final : public juce::Component
{
public:
    BrandingFooter();
    void paint(juce::Graphics&) override;
    void setCpuText(const juce::String& t) { if (t != cpuText) { cpuText = t; repaint(); } }

private:
    std::array<std::unique_ptr<juce::Drawable>, 3> logos;
    juce::String cpuText;
};

} // namespace ampsurd::ui
