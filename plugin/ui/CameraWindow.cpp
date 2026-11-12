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

// ---------------------------------------------------------------------------------------------
ScreenBoxView::ScreenBoxView(ScreenCapture& s, bool frame) : screen(s), frameOnly(frame)
{
    setOpaque(!frameOnly);
    setRepaintsOnMouseActivity(false);
    setTooltip(frameOnly ? "The camera picture (SWAP is on). Drag it to move it, drag its lower right corner to change its size. "
                           "SCREEN BOX shows / hides it, SWAP puts the camera back large - also while recording"
                         : "The screen you record. Drag it to move it, drag its lower right corner to change its size. "
                           "SCREEN BOX shows / hides it - also while recording");
    startTimerHz(15);
}

void ScreenBoxView::timerCallback()
{
    if (frameOnly) return;
    if (screen.getFrameCount() != shownFrame) { shownFrame = screen.getFrameCount(); repaint(); }
}

void ScreenBoxView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    if (frameOnly)
    {
        // SWAP: the camera picture is underneath - only the frame (as in the saved video) and the corner
        g.setColour(juce::Colour(0xffd6d6d6));
        g.drawRect(r, 2);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        for (int i = 0; i < 3; ++i)
            g.drawLine((float) getWidth() - 4.0f - (float) i * 4.0f, (float) getHeight() - 3.0f, (float) getWidth() - 3.0f,
                       (float) getHeight() - 4.0f - (float) i * 4.0f, 1.2f);
        return;
    }
    g.fillAll(juce::Colour(0xffd6d6d6)); // the light frame (as in the saved video)
    r.reduce(2, 2);
    g.setColour(juce::Colours::black);
    g.fillRect(r);
    const auto img = screen.latestFrame();
    if (img.isValid())
    {
        g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
        g.drawImageWithin(img, r.getX(), r.getY(), r.getWidth(), r.getHeight(), juce::RectanglePlacement::centred);
    }
    else
    {
        g.setColour(textFaint);
        g.setFont(Fonts::get().regular(11.0f));
        g.drawFittedText("waiting for the screen...", r.reduced(6), juce::Justification::centred, 2);
    }
    // the resize corner
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    for (int i = 0; i < 3; ++i)
        g.drawLine((float) getWidth() - 4.0f - (float) i * 4.0f, (float) getHeight() - 3.0f, (float) getWidth() - 3.0f,
                   (float) getHeight() - 4.0f - (float) i * 4.0f, 1.2f);
}

void ScreenBoxView::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(inCorner(e.getPosition()) ? juce::MouseCursor::BottomRightCornerResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void ScreenBoxView::mouseDown(const juce::MouseEvent& e)
{
    downPos = e.getScreenPosition();
    downBounds = getBounds();
    resizing = inCorner(e.getPosition());
}

void ScreenBoxView::mouseDrag(const juce::MouseEvent& e)
{
    const auto d = e.getScreenPosition() - downPos;
    const auto lim = limits ? limits() : getParentMonitorArea();
    auto b = downBounds;
    if (resizing)
    {
        // keeps the screen's proportions (16:9 for most screens); SWAP (the camera in the box): 16:9
        const auto img = frameOnly ? juce::Image() : screen.latestFrame();
        const double aspect = img.isValid() ? (double) img.getWidth() / juce::jmax(1, img.getHeight()) : 16.0 / 9.0;
        const int w = juce::jlimit(90, juce::jmax(90, lim.getRight() - b.getX()), downBounds.getWidth() + d.x);
        const int h = juce::jlimit(54, juce::jmax(54, lim.getBottom() - b.getY()), (int) std::lround((w - 4) / aspect) + 4);
        b.setSize(w, h);
    }
    else
    {
        b.setPosition(downBounds.getPosition() + d);
        b = b.constrainedWithin(lim);
    }
    setBounds(b);
    if (onMoved) onMoved(false);
}

void ScreenBoxView::mouseUp(const juce::MouseEvent&)
{
    if (onMoved) onMoved(true);
}

void ScreenPictureView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    const auto img = screen.latestFrame();
    if (img.isValid())
    {
        g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
        g.drawImageWithin(img, 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::centred);
    }
}

// ---------------------------------------------------------------------------------------------
CameraView::CameraView(VideoCapture& c, ScreenCapture& sc, PlayerRecorder& p) : screen(sc), camera(c), player(p)
{
    setOpaque(true);
    {
        juce::PropertiesFile pf(settingsOptions());
        const auto saved = juce::Rectangle<float>::fromString(pf.getValue("screenBoxRect"));
        if (saved.getWidth() > 0.05f && saved.getHeight() > 0.05f) boxRect = saved;
    }
    addChildComponent(bigScreen); // first: behind the camera picture
    addAndMakeVisible(screenBox);
    screenBox.setTooltip("SCREEN: record a screen of this computer as well (shown in a box over the camera picture). "
                         "The camera window itself is left out of the screen recording");
    screenBox.onChange = [this] { chooseScreen(screenBox.getSelectedId() - 2); };
    addAndMakeVisible(boxButton);
    boxButton.setClickingTogglesState(false);
    boxButton.setWantsKeyboardFocus(false);
    boxButton.setTooltip("SCREEN BOX: show or hide the screen in the video - any time, also while recording");
    boxButton.onClick = [this] { setBoxShown(!boxShown); };
    addAndMakeVisible(swapButton);
    swapButton.setClickingTogglesState(false);
    swapButton.setWantsKeyboardFocus(false);
    swapButton.setTooltip("SWAP: the screen large and the camera in the box - or back. Any time, also while recording: the saved video follows");
    swapButton.onClick = [this] { setSwapped(!swapped); };

    seekBar.setSliderStyle(juce::Slider::LinearHorizontal);
    seekBar.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    seekBar.setRange(0.0, 1.0, 0.0);
    seekBar.setWantsKeyboardFocus(false);
    seekBar.setEnabled(false);
    seekBar.setPopupDisplayEnabled(false, false, nullptr);
    seekBar.setTooltip("Where the video is. Click or drag to go to any place in it (also before PLAY: PLAY starts there)");
    seekBar.onValueChange = [this] {
        // dragging: the picture follows (a few times a second); the sound joins at the end of the drag
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (seekBar.isMouseButtonDown() && now - lastScrubMs > 120.0) { lastScrubMs = now; seekTo(seekBar.getValue(), false); }
    };
    seekBar.onDragEnd = [this] { seekTo(seekBar.getValue(), true); };
    addAndMakeVisible(seekBar);
    listScreens();
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
    recButton.setTooltip("Starts a video take at the playhead - where the vertical line is in the player's timeline (with count-in if it is on), recording the armed tracks. Press again to stop. "
                         "Keys: R or Ctrl+R start, Space stops");
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
    soundBox.addItem("WITH BACKING", 1);
    soundBox.addItem("WITHOUT BACKING", 2);
    soundBox.setSelectedId(1, juce::dontSendNotification);
    soundBox.setTooltip("The sound of the saved videos: your recorded tracks with the backing track, or without it (every take is saved automatically)");
    soundBox.onChange = [this] { if (onSoundChoice) onSoundChoice(soundBox.getSelectedId()); };
    addAndMakeVisible(watchBox);
    watchBox.setTooltip("What PLAY shows here: the last take (with AMPSURD's sound, before or after saving) or one of your saved videos");
    watchBox.onChange = [this] { stopPlayback(); seekStart = -1.0; };
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
    // the screen used last time (SCREEN BOX), if it is still there
    {
        const auto lastScreen = pf.getValue("videoScreen");
        for (int i = 1; i < screenBox.getNumItems() && lastScreen.isNotEmpty(); ++i)
            if (screenBox.getItemText(i) == lastScreen) { screenBox.setSelectedItemIndex(i, juce::dontSendNotification); chooseScreen(i - 1); }
    }
}

CameraView::~CameraView()
{
    boxView.reset();
    screen.close();
}

void CameraView::listScreens()
{
    screenBox.clear(juce::dontSendNotification);
    screenBox.addItem("NO SCREEN", 1);
    if (!ScreenCapture::isSupported())
    {
        screenBox.setTooltip("Recording a screen is not available on this system (Windows 10 / 11, macOS 12.3 or newer)");
        screenBox.setSelectedId(1, juce::dontSendNotification);
        screenBox.setEnabled(false);
        boxButton.setEnabled(false);
        return;
    }
    const auto names = ScreenCapture::screens();
    for (int i = 0; i < names.size(); ++i) screenBox.addItem(names[i], i + 2);
    screenBox.setSelectedId(screen.isOpen() ? screen.getOpenIndex() + 2 : 1, juce::dontSendNotification);
}

void CameraView::chooseScreen(int index)
{
    if (screen.isRecording()) return; // the combo is disabled while it records
    if (index < 0)
    {
        setSwapped(false);
        setBoxShown(false);
        screen.close();
        juce::PropertiesFile pf(settingsOptions());
        pf.setValue("videoScreen", juce::String());
        pf.saveIfNeeded();
        boxButton.setToggleState(false, juce::dontSendNotification);
        repaint();
        return;
    }
    const auto err = screen.open(index);
    if (err.isNotEmpty())
    {
        setStatus(err);
        screenBox.setSelectedId(1, juce::dontSendNotification);
        return;
    }
    excludedPeer = nullptr; // refresh() leaves the camera window out of this screen's recording
    {
        juce::PropertiesFile pf(settingsOptions());
        pf.setValue("videoScreen", screenBox.getText());
        pf.saveIfNeeded();
    }
    setBoxShown(true);
    // a video take running: the screen is recorded from now on (its place in time is known)
    if (camera.isRecording() && !screen.isRecording())
    {
        const auto e = screen.startRecording(player.getRecordingsFolder().getChildFile("Takes"));
        if (e.isNotEmpty()) setStatus(e);
    }
}

void CameraView::setBoxShown(bool on)
{
    if (on && !screen.isOpen()) { setStatus("Choose a SCREEN first (next to CAMERA)."); on = false; }
    boxShown = on;
    boxButton.setToggleState(on, juce::dontSendNotification);
    if (on && boxView == nullptr)
    {
        boxView = std::make_unique<ScreenBoxView>(screen, swapped);
        boxView->onMoved = [this](bool finished) { boxMoved(finished); };
       #if JUCE_MAC
        // a small window of its own, above the camera preview (a native view)
        boxView->addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
        boxView->setAlwaysOnTop(true);
        boxView->limits = [this] { return localAreaToGlobal(previewArea); };
       #else
        addChildComponent(*boxView);
        boxView->limits = [this] { return previewArea; };
       #endif
        placeBox();
        screen.excludeWindow(boxView.get());
    }
    if (boxView != nullptr)
    {
        boxView->setVisible(on && !playingBack && isShowing());
        if (on) boxView->toFront(false);
    }
    layoutPictures();
    pushKey();
    repaint();
}

void CameraView::setSwapped(bool on)
{
    if (on && !screen.isOpen()) { setStatus("Choose a SCREEN first (next to CAMERA)."); on = false; }
    swapButton.setToggleState(on, juce::dontSendNotification);
    if (on == swapped) return;
    swapped = on;
    rebuildBox(); // the box shows the screen, or (SWAP) only its frame over the camera picture
    layoutPictures();
    pushKey();
    if (isShowing())
        setStatus(on ? juce::String("SWAP: the screen large, you in the box (SCREEN BOX hides you).") : juce::String("The camera large again, the screen in the box."));
    repaint();
}

void CameraView::rebuildBox()
{
    if (boxView == nullptr) return;
    screen.excludeWindow(nullptr);
    boxView.reset();
    if (boxShown) { boxShown = false; setBoxShown(true); }
}

juce::Rectangle<int> CameraView::boxBoundsLocal() const
{
    const auto a = previewArea.toFloat();
    return juce::Rectangle<float>(a.getX() + boxRect.getX() * a.getWidth(), a.getY() + boxRect.getY() * a.getHeight(),
                                  boxRect.getWidth() * a.getWidth(), boxRect.getHeight() * a.getHeight()).toNearestInt()
        .constrainedWithin(previewArea);
}

void CameraView::layoutPictures()
{
    const bool show = !playingBack;
    if (swapped && screen.isOpen())
    {
        // SWAP: the screen fills the preview, the camera picture sits in the box (hidden with the box)
        bigScreen.setBounds(previewArea);
        bigScreen.setVisible(show);
        bigScreen.toBack();
        if (preview != nullptr)
        {
            preview->setBounds(boxBoundsLocal().reduced(2));
            preview->setVisible(show && boxShown);
        }
    }
    else
    {
        bigScreen.setVisible(false);
        if (preview != nullptr)
        {
            preview->setBounds(previewArea);
            preview->setVisible(show);
        }
    }
    if (boxView != nullptr && boxView->isVisible()) boxView->toFront(false);
}

void CameraView::placeBox()
{
    if (boxView == nullptr || previewArea.isEmpty()) return;
    auto b = boxBoundsLocal();
   #if JUCE_MAC
    b = localAreaToGlobal(b);
   #endif
    boxView->setBounds(b);
}

void CameraView::boxMoved(bool finished)
{
    if (boxView == nullptr || previewArea.isEmpty()) return;
    auto b = boxView->getBounds();
   #if JUCE_MAC
    b = getLocalArea(nullptr, b);
   #endif
    const auto a = previewArea.toFloat();
    boxRect = { (b.getX() - a.getX()) / a.getWidth(), (b.getY() - a.getY()) / a.getHeight(), b.getWidth() / a.getWidth(), b.getHeight() / a.getHeight() };
    if (swapped && preview != nullptr) preview->setBounds(boxBoundsLocal().reduced(2)); // the camera picture moves with its box
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (camera.isRecording() && (finished || now - lastKeyMs > 100.0)) pushKey(); // moving while recording: ~10 steps a second
    if (finished)
    {
        juce::PropertiesFile pf(settingsOptions());
        pf.setValue("screenBoxRect", boxRect.toString());
        pf.saveIfNeeded();
        if (!camera.isRecording() && onBoxMovedAfterTake) onBoxMovedAfterTake(boxRect);
    }
}

void CameraView::beginTakeKeys()
{
    liveKeys.clear();
    pushKey();
}

void CameraView::pushKey()
{
    if (!camera.isRecording()) return;
    lastKeyMs = juce::Time::getMillisecondCounterHiRes();
    ampsurd::screen::BoxKey k;
    k.visible = boxShown && screen.isOpen();
    k.swapped = swapped && screen.isOpen();
    k.rect = boxRect;
    liveKeys.push_back({ juce::Time::currentTimeMillis(), k });
}

double CameraView::previewAspect() const
{
    return previewArea.getHeight() > 0 ? (double) previewArea.getWidth() / previewArea.getHeight() : 16.0 / 9.0;
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
    r.removeFromTop(6);
    auto scr = r.removeFromTop(26);
    scr.removeFromLeft(70);
    screenBox.setBounds(scr.removeFromLeft(juce::jmin(320, juce::jmax(120, scr.getWidth() - 194))));
    scr.removeFromLeft(8);
    boxButton.setBounds(scr.removeFromLeft(juce::jmin(110, scr.getWidth())));
    scr.removeFromLeft(6);
    swapButton.setBounds(scr.removeFromLeft(juce::jmin(64, scr.getWidth())));
    r.removeFromBottom(22); // status line
    auto bottom = r.removeFromBottom(26);
    bottom.removeFromLeft(110);
    offsetSlider.setBounds(bottom.removeFromLeft(juce::jmin(300, bottom.getWidth())));
    r.removeFromBottom(24); // the note above the offset
    auto controls = r.removeFromBottom(34);
    auto watch = r.removeFromBottom(32);
    auto seek = r.removeFromBottom(24);
    seekTimeArea = seek.removeFromRight(92);
    seekBar.setBounds(seek.withSizeKeepingCentre(seek.getWidth(), 14));
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
    previewArea = r;
    layoutPictures();
    placeBox();
    if (boxView != nullptr && boxView->isVisible()) boxView->toFront(false);
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
    r.removeFromTop(6);
    g.drawText("SCREEN", r.removeFromTop(26).removeFromLeft(66), juce::Justification::centredLeft, false);
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
    r.removeFromBottom(24); // the bar under the video
    {
        auto mmss = [](double v) { const int t = (int) std::floor(juce::jmax(0.0, v)); return juce::String(t / 60) + ":" + juce::String(t % 60).paddedLeft('0', 2); };
        const double dur = videoDuration();
        g.setFont(Fonts::get().regular(12.0f));
        g.setColour(dur > 0.0 ? text : textFaint);
        g.drawText(dur > 0.0 ? mmss(seekBar.getValue()) + " / " + mmss(dur) : juce::String("-:-- / -:--"), seekTimeArea,
                   juce::Justification::centredRight, false);
        g.setFont(Fonts::get().label(11.0f));
    }
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
    screenBox.setEnabled(ScreenCapture::isSupported() && !screen.isRecording());
    if (screen.isOpen() && getPeer() != nullptr && getPeer() != excludedPeer)
    {
        excludedPeer = getPeer();
        screen.excludeWindow(getTopLevelComponent()); // the camera window is not in the screen recording
    }
    boxButton.setEnabled(screen.isOpen());
    swapButton.setEnabled(screen.isOpen());
    // the bar under the video: the chosen video's length (loaded in the background when stopped)
   #if JUCE_WINDOWS || JUCE_MAC
    if (!recording && !playingBack)
    {
        const auto chosenFile = selectedSavedVideo();
        const auto want = chosenFile != juce::File() ? chosenFile : (player.takeHasVideo() && !player.isBusy() ? player.getTakeVideo() : juce::File());
        if (want != juce::File() && want != loadedVideo && want != preloadTried && want.existsAsFile())
        {
            preloadTried = want;
            seekStart = -1.0;
            if (loadPlayback(want).failed()) loadedVideo = juce::File();
        }
    }
   #endif
    {
        const double dur = videoDuration();
        seekBar.setEnabled(dur > 0.0 && !recording);
        if (dur > 0.0 && !juce::exactlyEqual(seekBar.getMaximum(), dur)) seekBar.setRange(0.0, dur, 0.0);
        if (!seekBar.isMouseButtonDown())
        {
            double pos = juce::jmax(0.0, seekStart);
           #if JUCE_WINDOWS || JUCE_MAC
            if (playingBack && playback != nullptr) pos = playback->getPlayPosition();
           #endif
            if (dur <= 0.0) pos = 0.0;
            if (!juce::exactlyEqual(seekBar.getValue(), pos)) { seekBar.setValue(pos, juce::dontSendNotification); repaint(seekTimeArea); }
        }
        else repaint(seekTimeArea);
    }
    if (boxView != nullptr)
    {
        // hidden while a video plays here, when the window is minimised / hidden; on macOS it follows the window
        bool showing = isShowing();
        if (auto* peer = getPeer()) showing = showing && !peer->isMinimised();
        const bool want = boxShown && !playingBack && showing;
        if (boxView->isVisible() != want) { boxView->setVisible(want); if (want) boxView->toFront(false); }
       #if JUCE_MAC
        if (want && !boxView->isMouseButtonDown()) placeBox();
       #endif
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
        const auto r = loadPlayback(saved);
        if (r.failed()) { setStatus("The video cannot be played: " + r.getErrorMessage()); return; }
        player.stop();
        playback->setAudioVolume(1.0f);
        const double d = playback->getVideoDuration();
        playback->setPlayPosition(seekStart > 0.0 && (d <= 0.0 || seekStart < d - 0.2) ? seekStart : 0.0);
        if (preview != nullptr) preview->setVisible(false);
        bigScreen.setVisible(false);
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
        const auto r = loadPlayback(file);
        if (r.failed()) { setStatus("The video cannot be played: " + r.getErrorMessage()); return; }
    }
    playback->setAudioVolume(0.0f); // the camera has no sound worth hearing: AMPSURD plays it
    // video time v belongs to song time v - lead (lead = how much earlier the song's start is than frame 0)
    const double lead = player.videoAudioLeadSeconds(0);
    double song0 = juce::jmax(0.0, -lead), video0 = song0 + lead;
    // the bar under the video chose where to start
    const double d = playback->getVideoDuration();
    if (seekStart > video0 && (d <= 0.0 || seekStart < d - 0.2)) { video0 = seekStart; song0 = video0 - lead; }
    player.stop();
    player.setPositionSeconds(song0);
    playback->setPlayPosition(juce::jmax(0.0, video0));
    if (preview != nullptr) preview->setVisible(false);
    bigScreen.setVisible(false);
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

#if JUCE_WINDOWS || JUCE_MAC
juce::Result CameraView::loadPlayback(const juce::File& f)
{
    // The player component must be IN the window before load(): on Windows it opens its own native video window
    // inside AMPSURD's, and without a window to put it in load() failed with "Can't create window" (owner 2026-10-29).
    if (playback != nullptr) removeChildComponent(playback.get());
    playback = std::make_unique<juce::VideoComponent>(false);
    addChildComponent(*playback);
    resized(); // its place: where the camera picture is
    const auto r = playback->load(f);
    if (r.failed())
    {
        removeChildComponent(playback.get());
        playback.reset();
        loadedVideo = juce::File();
        return r;
    }
    loadedVideo = f;
    return r;
}
#endif

void CameraView::stopPlayback()
{
    if (!playingBack) return;
    playingBack = false;
    const bool wasFile = std::exchange(playingFile, false);
   #if JUCE_WINDOWS || JUCE_MAC
    if (playback != nullptr)
    {
        // the bar keeps the place: PLAY goes on from there (from the start when the video had ended)
        const double d = playback->getVideoDuration(), at = playback->getPlayPosition();
        seekStart = (d > 0.0 && at < d - 0.2) ? at : -1.0;
        playback->stop();
        playback->setVisible(false);
    }
   #endif
    if (!wasFile && player.getState() == PlayerRecorder::State::playing) player.stop();
    layoutPictures();
    repaint();
}

double CameraView::videoDuration() const
{
   #if JUCE_WINDOWS || JUCE_MAC
    if (playback != nullptr && loadedVideo != juce::File())
    {
        const auto chosenFile = selectedSavedVideo();
        const auto want = chosenFile != juce::File() ? chosenFile : player.getTakeVideo();
        if (want == loadedVideo) return playback->getVideoDuration();
    }
   #endif
    return 0.0;
}

void CameraView::seekTo(double v, bool withSound)
{
   #if JUCE_WINDOWS || JUCE_MAC
    if (playingBack && playback != nullptr)
    {
        playback->setPlayPosition(juce::jmax(0.0, v));
        if (!playingFile && withSound)
        {
            // the take: AMPSURD's sound follows (video time v = song time v - lead)
            const double lead = player.videoAudioLeadSeconds(0);
            player.setPositionSeconds(juce::jmax(0.0, v - lead));
        }
        return;
    }
   #endif
    juce::ignoreUnused(withSound);
    seekStart = v; // stopped: PLAY starts here
    repaint(seekTimeArea);
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

CameraWindow::CameraWindow(VideoCapture& c, ScreenCapture& sc, PlayerRecorder& p, std::function<void()> onClosed)
    : juce::DocumentWindow("AMPSURD - Camera", background, juce::DocumentWindow::allButtons), // - MAXIMISE X (owner 2026-11-12)
      closed(std::move(onClosed))
{
    setLookAndFeel(&lnf);
    auto v = std::make_unique<CameraView>(c, sc, p);
    contentView = v.get();
    v->setSize(600, 646);
    setContentOwned(v.release(), true);
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(480, 506, 16384, 16384); // maximised: the whole screen
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
