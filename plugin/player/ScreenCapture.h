#pragma once

// SCREEN BOX (standalone, VIDEO takes): records a computer screen while the camera records you. The screen's picture
// is shown live in a box over the camera preview and recorded into its own file (H.264, at most 1280 x 720, 15 frames
// per second); the two are combined when the MP4 is saved (VideoMux + ScreenOverlay).
//   Windows 10 / 11: DXGI desktop duplication + Media Foundation. The camera window is left out of the recording.
//   macOS 12.3 or newer: ScreenCaptureKit + AVFoundation (the first time, macOS asks for the permission to record
//   the screen). Older macOS: not available.
//   Linux (tests): AMPSURD_FAKE_SCREEN=1 gives a test picture (no recording).
// Message thread for everything except latestFrame() (any thread).

#include <atomic>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

class ScreenCapture
{
public:
    ScreenCapture();
    ~ScreenCapture();

    static bool isSupported();
    static juce::StringArray screens(); // "Screen 1 (1920 x 1080)" ...

    juce::String open(int index);       // starts capturing (for the box); empty = ok, otherwise the error to show
    void close();
    bool isOpen() const;
    int getOpenIndex() const { return openIndex; }

    // the newest picture (about 480 px wide) for the box; null before the first frame
    juce::Image latestFrame() const;
    int getFrameCount() const { return frames.load(); } // changes with every new picture

    juce::String startRecording(const juce::File& folder); // empty = ok
    // stops; returns the first recorded frame's wall clock (epoch ms) and the file (none when nothing was recorded)
    juce::int64 stopRecording(juce::File& recordedFile);
    bool isRecording() const { return recording; }

    // a window that must not appear in the recording (the camera window, the box)
    void excludeWindow(juce::Component* topLevelWindow);

    std::function<void(const juce::String&)> onError; // called on the message thread

    // a preview picture (about 480 px wide) from a BGRA frame
    static juce::Image makePreview(const uint8_t* bgra, int w, int h, int pitch);

    struct Impl; // the platform's part (also used by its frame callback)
    void publishPreview(const juce::Image& img);       // from the capture thread
    void reportError(const juce::String& e);           // from any thread

private:
    std::unique_ptr<Impl> impl;
    int openIndex = -1;
    bool recording = false;
    mutable juce::SpinLock previewLock;
    juce::Image preview;
    std::atomic<int> frames { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
};
