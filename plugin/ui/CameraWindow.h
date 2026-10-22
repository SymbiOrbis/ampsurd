#pragma once

// The camera's own window (standalone player, VIDEO on): choose the camera, see yourself, record and save
// the video take right here (REC / STOP, SAVE MP4 with GUITAR + BACKING or GUITAR ONLY), fine-tune SOUND
// OFFSET. A separate, movable window, so the player keeps its room (and the preview can sit on a second
// screen). Closing it switches VIDEO off.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../player/PlayerRecorder.h"
#include "../player/VideoCapture.h"
#include "Theme.h"

namespace ampsurd::ui
{
class CameraView final : public juce::Component
{
public:
    CameraView(VideoCapture&, PlayerRecorder&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(); // ~30 Hz from the player panel
    juce::String openCamera(int index);
    void setStatus(const juce::String& s);      // the player's status line, shown here too
    void setSoundChoice(int id);                // 1 = GUITAR + BACKING, 2 = GUITAR ONLY (as in the player)
    std::function<void()> onRec, onSave;        // REC / STOP and SAVE MP4 (the player panel does the work)
    std::function<void(int)> onSoundChoice;

private:
    void listCameras();
    VideoCapture& camera;
    PlayerRecorder& player;
    juce::ComboBox cameraBox, soundBox;
    juce::TextButton recButton { "REC" }, saveButton { "SAVE MP4..." };
    juce::String status;
    juce::Slider offsetSlider;
    std::unique_ptr<juce::Component> preview;
    juce::String message;
    bool recordingShown = false;
};

class CameraWindow final : public juce::DocumentWindow
{
public:
    CameraWindow(VideoCapture&, PlayerRecorder&, std::function<void()> onClosed);
    ~CameraWindow() override;
    void closeButtonPressed() override { if (closed) closed(); }
    CameraView& view() { return *contentView; }

private:
    LookAndFeel lnf; // a window of its own: AMPSURD's look set here
    CameraView* contentView = nullptr;
    std::function<void()> closed;
};
} // namespace ampsurd::ui
