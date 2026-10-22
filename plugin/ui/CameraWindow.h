#pragma once

// The camera's own window (standalone player, VIDEO on): choose the camera, see yourself, fine-tune
// SOUND OFFSET. A separate, movable window, so the player keeps its room (and the preview can sit on a
// second screen). Closing it switches VIDEO off.

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

private:
    void listCameras();
    VideoCapture& camera;
    PlayerRecorder& player;
    juce::ComboBox cameraBox;
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
