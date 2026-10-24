#include "CameraWindow.h"

#include "Theme.h"

#include <algorithm>
#include <utility>

namespace ampsurd::ui
{
using namespace colours;

namespace
{
juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "AMPSURD"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
    return o;
}
} // namespace

CameraView::CameraView(VideoCapture& c, PlayerRecorder& p) : camera(c), player(p)
{
    setOpaque(true);
    addAndMakeVisible(cameraBox);
    cameraBox.setTooltip("The camera that films you while you record a VIDEO take");
    cameraBox.onChange = [this] {
        const int i = cameraBox.getSelectedItemIndex();
        if (i >= 0 && i != camera.getOpenIndex()) message = openCamera(i);
        repaint();
    };

    offsetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    offsetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    offsetSlider.setRange(-500.0, 500.0, 1.0);
    offsetSlider.setTextValueSuffix(" ms");
    offsetSlider.setDoubleClickReturnValue(true, 0.0);
    juce::PropertiesFile pf(settingsOptions());
    offsetSlider.setValue(pf.getDoubleValue("videoOffsetMs", 0.0), juce::dontSendNotification);
    player.setVideoOffsetMs((float) offsetSlider.getValue());
    offsetSlider.onValueChange = [this] {
        player.setVideoOffsetMs((float) offsetSlider.getValue());
        juce::PropertiesFile f(settingsOptions());
        f.setValue("videoOffsetMs", offsetSlider.getValue());
        f.saveIfNeeded();
    };
    offsetSlider.setTooltip("If the picture and the sound of saved videos are not together: + moves the sound later, - earlier. "
                            "Applies when saving (also to takes already recorded). Double-click: 0");
    addAndMakeVisible(offsetSlider);

    addAndMakeVisible(recButton);
    recButton.setTooltip("Starts a video take from the beginning (with count-in if it is on). Press again to stop. Shortcut: R");
    recButton.onClick = [this] { if (onRec) onRec(); };
    addAndMakeVisible(playButton);
    playButton.setTooltip("Plays the video take with AMPSURD's sound (the same as the saved video will have). Again: stop");
    playButton.onClick = [this] { if (playingBack) stopPlayback(); else startPlayback(); };
    addAndMakeVisible(showButton);
    showButton.setTooltip("Shows the saved video chosen in WATCH (or the last saved one) in Explorer / Finder");
    showButton.onClick = [this] {
        const auto f = selectedSavedVideo();
        if (f.existsAsFile()) f.revealToUser();
        else if (onShowFile) onShowFile();
    };
    addAndMakeVisible(soundBox);
    soundBox.addItem("GUITAR + BACKING", 1);
    soundBox.addItem("GUITAR ONLY", 2);
    soundBox.setSelectedId(1, juce::dontSendNotification);
    soundBox.setTooltip("The sound of the saved videos: guitar with the backing track, or guitar only (every take is saved automatically)");
    soundBox.onChange = [this] { if (onSoundChoice) onSoundChoice(soundBox.getSelectedId()); };
    addAndMakeVisible(watchBox);
    watchBox.setTooltip("What PLAY shows here: the last take (with AMPSURD's sound, before or after saving) or one of your saved videos");
    watchBox.onChange = [this] { stopPlayback(); };
    refreshVideoList();

    listCameras();
    if (!VideoCapture::isSupported()) message = "Video recording is not available on this system.";
    else if (cameraBox.getNumItems() == 0) message = "No camera found. Connect one and open this window again.";
    else
    {
        // the camera used last time, else the first one
        const auto last = pf.getValue("videoCamera");
        int index = 0;
        for (int i = 0; i < cameraBox.getNumItems(); ++i)
            if (cameraBox.getItemText(i) == last) index = i;
        cameraBox.setSelectedItemIndex(index, juce::dontSendNotification);
        message = openCamera(index);
    }
}

void CameraView::listCameras()
{
    cameraBox.clear(juce::dontSendNotification);
    const auto names = VideoCapture::cameras();
    for (int i = 0; i < names.size(); ++i)
        cameraBox.addItem(names[i], i + 1);
}

juce::String CameraView::openCamera(int index)
{
    if (camera.isRecording()) return "The camera cannot be changed while a video take is being recorded.";
    preview.reset();
    const auto err = camera.open(index);
    if (err.isNotEmpty()) return err;
    preview = camera.createPreview();
    if (preview != nullptr) addAndMakeVisible(*preview);
    juce::PropertiesFile pf(settingsOptions());
    pf.setValue("videoCamera", cameraBox.getItemText(index));
    pf.saveIfNeeded();
    resized();
    return {};
}

void CameraView::resized()
{
    auto r = getLocalBounds().reduced(12, 10);
    auto top = r.removeFromTop(26);
    top.removeFromLeft(70);
    cameraBox.setBounds(top.removeFromLeft(juce::jmin(320, top.getWidth())));
    r.removeFromBottom(22); // status line
    auto bottom = r.removeFromBottom(26);
    bottom.removeFromLeft(110);
    offsetSlider.setBounds(bottom.removeFromLeft(juce::jmin(300, bottom.getWidth())));
    r.removeFromBottom(24); // the note above the offset
    auto controls = r.removeFromBottom(34);
    auto watch = r.removeFromBottom(32);
    watch.removeFromLeft(70);
    const int ww = juce::jmin(420, watch.getWidth());
    watchBox.setBounds(watch.removeFromLeft(ww).withSizeKeepingCentre(ww, 26));
    recButton.setBounds(controls.removeFromLeft(96).withSizeKeepingCentre(96, 30));
    controls.removeFromLeft(8);
    playButton.setBounds(controls.removeFromLeft(86).withSizeKeepingCentre(86, 30));
    controls.removeFromLeft(16);
    const int sw = juce::jmin(170, juce::jmax(0, controls.getWidth() - 104));
    soundBox.setBounds(controls.removeFromLeft(sw).withSizeKeepingCentre(sw, 26));
    controls.removeFromLeft(8);
    const int bw = juce::jmin(96, controls.getWidth());
    showButton.setBounds(controls.removeFromLeft(bw).withSizeKeepingCentre(bw, 30));
    r.reduce(0, 8);
    if (preview != nullptr) preview->setBounds(r);
   #if JUCE_WINDOWS || JUCE_MAC
    if (playback != nullptr) playback->setBounds(r);
   #endif
}

void CameraView::paint(juce::Graphics& g)
{
    g.fillAll(background);
    auto r = getLocalBounds().reduced(12, 10);
    g.setColour(textDim);
    g.setFont(Fonts::get().label(11.0f));
    g.drawText("CAMERA", r.removeFromTop(26).removeFromLeft(66), juce::Justification::centredLeft, false);
    {
        auto st = r.removeFromBottom(22);
        g.setFont(Fonts::get().regular(12.0f));
        g.setColour(text);
        g.drawFittedText(status, st, juce::Justification::centredLeft, 1, 0.85f);
        g.setFont(Fonts::get().label(11.0f));
        g.setColour(textDim);
    }
    auto bottom = r.removeFromBottom(26);
    g.drawText("SOUND OFFSET", bottom.removeFromLeft(106), juce::Justification::centredLeft, false);
    auto note = r.removeFromBottom(24);
    g.setFont(Fonts::get().regular(12.0f));
    g.setColour(textFaint);
    g.drawText("The sound of the video is AMPSURD's own (as when you save audio), not the camera's microphone.", note,
               juce::Justification::centredLeft, true);
    r.removeFromBottom(34); // REC / SAVE row
    g.setFont(Fonts::get().label(11.0f));
    g.setColour(textDim);
    g.drawText("WATCH", r.removeFromBottom(32).removeFromLeft(66), juce::Justification::centredLeft, false);
    r.reduce(0, 8);
    g.setColour(line);
    g.drawRect(r);
    if (preview == nullptr || message.isNotEmpty())
    {
        g.setColour(textDim);
        g.setFont(Fonts::get().regular(13.0f));
        g.drawFittedText(message.isNotEmpty() ? message : juce::String("No camera"), r.reduced(20), juce::Justification::centred, 3);
    }
    if (camera.isRecording())
    {
        // shown as text, not only by colour
        const auto tag = juce::Rectangle<int>(r.getRight() - 150, r.getY() + 8, 140, 22);
        g.setColour(accent);
        g.fillRoundedRectangle(tag.toFloat(), 3.0f);
        g.setColour(juce::Colours::white);
        g.setFont(Fonts::get().semibold(12.0f));
        const int secs = (int) player.getPositionSeconds();
        g.drawText(juce::String::charToString((juce::juce_wchar) 0x25CF) + " REC VIDEO " + juce::String(secs / 60) + ":"
                       + juce::String(secs % 60).paddedLeft('0', 2),
                   tag, juce::Justification::centred, false);
    }
    if (notice.isNotEmpty())
    {
        // over the picture, at the bottom: shown as text, not only by colour
        g.setFont(Fonts::get().semibold(13.0f));
        const int w = juce::jmin(r.getWidth() - 20, juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), notice) + 40);
        const auto box = juce::Rectangle<int>(r.getCentreX() - w / 2, r.getBottom() - 46, w, 34);
        g.setColour(noticeOk ? juce::Colour(0xee1f6f43) : accent.withAlpha(0.93f));
        g.fillRoundedRectangle(box.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.drawFittedText(notice, box.reduced(12, 0), juce::Justification::centred, 1, 0.8f);
    }
}

void CameraView::showNotice(const juce::String& text, double seconds, bool ok)
{
    notice = text;
    noticeOk = ok;
    noticeUntil = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    repaint();
}

void CameraView::refresh()
{
    using S = PlayerRecorder::State;
    const auto st = player.getState();
    const bool recording = st == S::recording || st == S::pausedRec;
    const juce::String recText = player.isCountingIn() ? "COUNT-IN " + juce::String(player.getCountInBeatsLeft())
                               : recording ? juce::String::charToString((juce::juce_wchar) 0x25A0) + " STOP"
                                           : juce::String::charToString((juce::juce_wchar) 0x25CF) + " REC";
    const juce::String recShown = (!recording && !player.isCountingIn() && replaceArmed && replaceArmed()) ? juce::String("REPLACE?") : recText;
    if (recButton.getButtonText() != recShown) recButton.setButtonText(recShown);
    recButton.setToggleState(recording, juce::dontSendNotification);
    recButton.setEnabled(camera.isOpen() || recording);
    // playback ends with the video, or when the player was stopped elsewhere
   #if JUCE_WINDOWS || JUCE_MAC
    // (the video's own isPlaying() can lag behind on Windows: it ends at its last frame or when the player stops)
    if (playingBack && (playback == nullptr || (!playingFile && st != S::playing)
                        || (playback->getVideoDuration() > 0.0 && playback->getPlayPosition() >= playback->getVideoDuration() - 0.05)))
        stopPlayback();
   #endif
    const juce::String playText = playingBack ? juce::String::charToString((juce::juce_wchar) 0x25A0) + " STOP"
                                              : juce::String::charToString((juce::juce_wchar) 0x25B6) + " PLAY";
    if (playButton.getButtonText() != playText) playButton.setButtonText(playText);
    playButton.setToggleState(playingBack, juce::dontSendNotification);
    if (++listCheck >= 60) { listCheck = 0; if (!recording) refreshVideoList(); } // ~every 2 s: new / deleted videos
    const auto chosen = selectedSavedVideo();
    playButton.setEnabled(!recording && (chosen != juce::File() ? chosen.existsAsFile() : player.takeHasVideo()));
    watchBox.setEnabled(!recording);
    if (noticeUntil > 0.0 && juce::Time::getMillisecondCounterHiRes() > noticeUntil) { noticeUntil = 0.0; notice.clear(); repaint(); }
    showButton.setEnabled(chosen.existsAsFile() || (hasSavedFile && hasSavedFile()));
    soundBox.setEnabled(!recording);
    if (camera.isRecording() != recordingShown)
    {
        recordingShown = camera.isRecording();
        cameraBox.setEnabled(!recordingShown);
    }
    if (recordingShown) repaint(); // the running time
}

void CameraView::startPlayback()
{
   #if JUCE_WINDOWS || JUCE_MAC
    const auto saved = selectedSavedVideo();
    if (saved != juce::File())
    {
        // a saved video: it has its sound inside (AMPSURD's, as saved) - played as it is, the player stays out
        if (!saved.existsAsFile()) { setStatus("This video is no longer there."); refreshVideoList(); return; }
        playback = std::make_unique<juce::VideoComponent>(false);
        const auto r = playback->load(saved);
        if (r.failed()) { playback.reset(); loadedVideo = juce::File(); setStatus("The video cannot be played: " + r.getErrorMessage()); return; }
        loadedVideo = saved;
        addChildComponent(*playback);
        resized();
        player.stop();
        playback->setAudioVolume(1.0f);
        playback->setPlayPosition(0.0);
        if (preview != nullptr) preview->setVisible(false);
        playback->setVisible(true);
        playback->play();
        playingBack = playingFile = true;
        setStatus("Playing " + saved.getFileName() + ". STOP: the same button.");
        repaint();
        return;
    }
    const auto file = player.getTakeVideo();
    if (!player.takeHasVideo()) { setStatus("No video take yet."); return; }
    if (player.isBusy()) { setStatus("Still saving - PLAY works again in a moment."); return; }
    if (playback == nullptr || loadedVideo != file)
    {
        playback = std::make_unique<juce::VideoComponent>(false);
        const auto r = playback->load(file);
        if (r.failed()) { playback.reset(); setStatus("The video cannot be played: " + r.getErrorMessage()); return; }
        loadedVideo = file;
        addChildComponent(*playback);
        resized();
    }
    playback->setAudioVolume(0.0f); // the camera has no sound worth hearing: AMPSURD plays it
    // video time v belongs to song time v - lead (lead = how much earlier the song's start is than frame 0)
    const double lead = player.videoAudioLeadSeconds(0);
    const double song0 = juce::jmax(0.0, -lead), video0 = song0 + lead;
    player.stop();
    player.setPositionSeconds(song0);
    playback->setPlayPosition(juce::jmax(0.0, video0));
    if (preview != nullptr) preview->setVisible(false);
    playback->setVisible(true);
    playback->play();
    player.play();
    playingBack = true;
    setStatus("Playing the video take with AMPSURD's sound. STOP: the same button.");
    repaint();
   #else
    setStatus("Video playback is not available on this system.");
   #endif
}

void CameraView::stopPlayback()
{
    if (!playingBack) return;
    playingBack = false;
    const bool wasFile = std::exchange(playingFile, false);
   #if JUCE_WINDOWS || JUCE_MAC
    if (playback != nullptr)
    {
        playback->stop();
        playback->setVisible(false);
    }
   #endif
    if (!wasFile && player.getState() == PlayerRecorder::State::playing) player.stop();
    if (preview != nullptr) preview->setVisible(true);
    repaint();
}

void CameraView::refreshVideoList()
{
    auto files = player.getRecordingsFolder().findChildFiles(juce::File::findFiles, false, "*.mp4");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        return a.getLastModificationTime() > b.getLastModificationTime();
    });
    juce::String key;
    for (auto& f : files) key << f.getFileName() << '|' << f.getLastModificationTime().toMilliseconds() << '\n';
    if (key == videoListKey && watchBox.getNumItems() > 0) return;
    videoListKey = key;
    const auto keep = selectedSavedVideo(); // stays chosen if it is still there
    savedVideos = files;
    watchBox.clear(juce::dontSendNotification);
    watchBox.addItem("Last take (with AMPSURD's sound)", 1);
    if (!savedVideos.isEmpty()) watchBox.addSectionHeading("Saved videos (Documents / AMPSURD / Recordings)");
    for (int i = 0; i < savedVideos.size(); ++i) watchBox.addItem(savedVideos[i].getFileNameWithoutExtension(), i + 2);
    const int idx = savedVideos.indexOf(keep);
    watchBox.setSelectedId(idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);
}

juce::File CameraView::selectedSavedVideo() const
{
    const int id = watchBox.getSelectedId();
    return id >= 2 && id - 2 < savedVideos.size() ? savedVideos[id - 2] : juce::File();
}

void CameraView::setStatus(const juce::String& s)
{
    if (s == status) return;
    status = s;
    repaint();
}

void CameraView::setSoundChoice(int id)
{
    if (soundBox.getSelectedId() != id) soundBox.setSelectedId(id, juce::dontSendNotification);
}

CameraWindow::CameraWindow(VideoCapture& c, PlayerRecorder& p, std::function<void()> onClosed)
    : juce::DocumentWindow("AMPSURD - Camera", background, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      closed(std::move(onClosed))
{
    setLookAndFeel(&lnf);
    auto v = std::make_unique<CameraView>(c, p);
    contentView = v.get();
    v->setSize(600, 590);
    setContentOwned(v.release(), true);
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(480, 450, 1920, 1400);
    centreWithSize(getWidth(), getHeight());
    setAlwaysOnTop(true); // stays in front of AMPSURD's window while you play / save (owner, 2026-10-22)
    setVisible(true);
}
CameraWindow::~CameraWindow()
{
    if (contentView != nullptr) contentView->stopPlayback();
    clearContentComponent();
    setLookAndFeel(nullptr);
}
} // namespace ampsurd::ui
