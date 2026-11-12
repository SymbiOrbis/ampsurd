#pragma once

// The camera's own window (standalone player, VIDEO on): choose the camera, see yourself, record, watch and
// the video takes right here (REC / STOP; every take is saved automatically as an MP4 with GUITAR + BACKING or GUITAR
// ONLY into Recordings, a notice says so; WATCH + PLAY: the last take or any saved video; SHOW FILE), fine-tune SOUND
// OFFSET. For spontaneous ideas: no SAVE button (owner, 2026-10-24). A separate, movable window, so the player keeps its room (and the preview can sit on a second
// screen). Closing it switches VIDEO off.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../player/PlayerRecorder.h"
#include "../player/ScreenCapture.h"
#include "../player/ScreenOverlay.h"
#include "../player/VideoCapture.h"
#include "Theme.h"

namespace ampsurd::ui
{
// SCREEN BOX (owner 2026-11-12): the chosen screen's live picture in a box over the camera picture - drag it to
// move it, drag its lower right corner to resize it. A child of the camera window; on macOS a small window of its
// own (the camera preview is a native view that would cover a child).
class ScreenBoxView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    // frameOnly (SWAP): only the frame and the resize corner are drawn - the camera picture shows through it
    ScreenBoxView(ScreenCapture&, bool frameOnly = false);
    ~ScreenBoxView() override { stopTimer(); }
    std::function<void(bool finished)> onMoved;   // while dragging (false) and at the end (true)
    std::function<juce::Rectangle<int>()> limits; // where it may be: the preview area, in its own parent's / the screen's coordinates
    bool isFrameOnly() const { return frameOnly; }
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    bool inCorner(juce::Point<int> p) const { return p.x > getWidth() - 16 && p.y > getHeight() - 16; }
    ScreenCapture& screen;
    const bool frameOnly;
    int shownFrame = -1;
    juce::Point<int> downPos;
    juce::Rectangle<int> downBounds;
    bool resizing = false;
};

// SWAP: the screen's live picture large, behind the camera picture (which is then in the box)
class ScreenPictureView final : public juce::Component, private juce::Timer
{
public:
    explicit ScreenPictureView(ScreenCapture& s) : screen(s) { setOpaque(true); setInterceptsMouseClicks(false, false); startTimerHz(15); }
    ~ScreenPictureView() override { stopTimer(); }
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override { if (screen.getFrameCount() != shownFrame) { shownFrame = screen.getFrameCount(); repaint(); } }
    ScreenCapture& screen;
    int shownFrame = -1;
};

class CameraView final : public juce::Component
{
public:
    CameraView(VideoCapture&, ScreenCapture&, PlayerRecorder&);
    ~CameraView() override;
    // SCREEN BOX during a video take: the box's states (shown / where) with their wall clock (epoch ms)
    void beginTakeKeys();
    struct TimedKey { juce::int64 epochMs; ampsurd::screen::BoxKey key; };
    std::vector<TimedKey> takeKeys() const { return liveKeys; }
    double previewAspect() const;
    std::function<void(juce::Rectangle<float>)> onBoxMovedAfterTake; // the box moved while not recording
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(); // ~30 Hz from the player panel
    juce::String openCamera(int index);
    void setStatus(const juce::String& s);      // the player's status line, shown here too
    void setSoundChoice(int id);                // 1 = GUITAR + BACKING, 2 = GUITAR ONLY (as in the player)
    std::function<void()> onRec, onShowFile;    // REC / STOP, SHOW FILE (the player panel does the work)
    std::function<bool(const juce::KeyPress&)> onKey; // R / Ctrl+R / Space here too: the main window's keys
    bool keyPressed(const juce::KeyPress& k) override { return onKey && onKey(k); }
    std::function<void(int)> onSoundChoice;
    std::function<bool()> replaceArmed;         // REC pressed once with a take there: shows REPLACE?
    std::function<bool()> hasSavedFile;         // SHOW FILE is enabled
    void stopPlayback();                        // also when recording starts or the window closes
    void refreshVideoList();                    // the saved videos in Recordings (also after a save)
    juce::File selectedSavedVideo() const;      // the saved video chosen in WATCH (none = the last take)
    bool isPlayingBack() const { return playingBack; }
    // a notice over the picture ("SAVING VIDEO...", "VIDEO SAVED ..."), for `seconds`; ok = false: an error
    void showNotice(const juce::String& text, double seconds, bool ok = true);

private:
    void listCameras();
    void listScreens();
    void chooseScreen(int index);      // -1 = none
    void setBoxShown(bool on);
    void setSwapped(bool on);          // SWAP: the screen large, the camera in the box
    void rebuildBox();                 // the box again (its kind follows SWAP)
    void layoutPictures();             // camera / screen / box / playback: where and whether shown
    void placeBox();                   // the box where boxRect says (in the preview area)
    juce::Rectangle<int> boxBoundsLocal() const; // the box in this view's coordinates
    void boxMoved(bool finished);
    void pushKey();                    // while recording: the box's state now
    ScreenCapture& screen;
    juce::ComboBox screenBox;
    juce::TextButton boxButton { "SCREEN BOX" }, swapButton { "SWAP" };
    std::unique_ptr<ScreenBoxView> boxView;
    ScreenPictureView bigScreen { screen };
    bool swapped = false;
    juce::Rectangle<float> boxRect { 0.63f, 0.66f, 0.34f, 0.29f }; // in the preview area, 0..1
    bool boxShown = false;
    juce::Rectangle<int> previewArea;
    std::vector<TimedKey> liveKeys;
    double lastKeyMs = 0.0;
    void* excludedPeer = nullptr;      // the window left out of the screen recording (again when it changes)
    void startPlayback();
    VideoCapture& camera;
    PlayerRecorder& player;
    juce::ComboBox cameraBox, soundBox, watchBox; // WATCH: the last take, or one of the saved videos
    juce::Array<juce::File> savedVideos;          // watchBox item i + 2 = savedVideos[i] (newest first)
    juce::String videoListKey;                    // changes when the Recordings folder's videos change
    int listCheck = 0;
    bool playingFile = false;                     // playing a saved video (its own sound), not the take
    juce::TextButton recButton { "REC" }, playButton { "PLAY" }, showButton { "SHOW FILE" };
    juce::String notice;
    double noticeUntil = 0.0;
    bool noticeOk = true;
   #if JUCE_WINDOWS || JUCE_MAC
    juce::Result loadPlayback(const juce::File& f); // creates the player in the window, then loads (Windows needs both)
    std::unique_ptr<juce::VideoComponent> playback; // the recorded take (muted; the sound comes from the player)
    juce::File loadedVideo;
    juce::File preloadTried;                        // loaded in the background once (for the bar under the video)
   #endif
    bool playingBack = false;
    // the bar under the video (owner 2026-11-12): where the video is, drag / click to go anywhere in it
    juce::Slider seekBar;
    juce::Rectangle<int> seekTimeArea;
    double seekStart = -1.0;           // stopped: PLAY starts here (video seconds); -1 = from the beginning
    double lastScrubMs = 0.0;
    void seekTo(double videoSeconds, bool withSound);
    double videoDuration() const;
    juce::String status;
    juce::Slider offsetSlider;
    std::unique_ptr<juce::Component> preview;
    juce::String message;
    bool recordingShown = false;
};

class CameraWindow final : public juce::DocumentWindow
{
public:
    CameraWindow(VideoCapture&, ScreenCapture&, PlayerRecorder&, std::function<void()> onClosed);
    ~CameraWindow() override;
    void closeButtonPressed() override { if (closed) closed(); }
    CameraView& view() { return *contentView; }
    bool keyPressed(const juce::KeyPress& k) override { return contentView != nullptr && contentView->keyPressed(k); }

private:
    LookAndFeel lnf; // a window of its own: AMPSURD's look set here
    CameraView* contentView = nullptr;
    std::function<void()> closed;
};
} // namespace ampsurd::ui
