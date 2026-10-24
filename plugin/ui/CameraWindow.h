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
    std::function<void()> onRec, onShowFile;    // REC / STOP, SHOW FILE (the player panel does the work)
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
    std::unique_ptr<juce::VideoComponent> playback; // the recorded take (muted; the sound comes from the player)
    juce::File loadedVideo;
   #endif
    bool playingBack = false;
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
