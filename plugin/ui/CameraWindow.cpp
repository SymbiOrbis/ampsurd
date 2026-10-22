#include "CameraWindow.h"

#include "Theme.h"

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
    auto bottom = r.removeFromBottom(26);
    bottom.removeFromLeft(110);
    offsetSlider.setBounds(bottom.removeFromLeft(juce::jmin(300, bottom.getWidth())));
    r.removeFromBottom(26); // the note above the offset
    r.reduce(0, 8);
    if (preview != nullptr) preview->setBounds(r);
}

void CameraView::paint(juce::Graphics& g)
{
    g.fillAll(background);
    auto r = getLocalBounds().reduced(12, 10);
    g.setColour(textDim);
    g.setFont(Fonts::get().label(11.0f));
    g.drawText("CAMERA", r.removeFromTop(26).removeFromLeft(66), juce::Justification::centredLeft, false);
    auto bottom = r.removeFromBottom(26);
    g.drawText("SOUND OFFSET", bottom.removeFromLeft(106), juce::Justification::centredLeft, false);
    auto note = r.removeFromBottom(26);
    g.setFont(Fonts::get().regular(12.0f));
    g.setColour(textFaint);
    g.drawText("The sound of the video is AMPSURD's own (as when you save audio), not the camera's microphone.", note,
               juce::Justification::centredLeft, true);
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
        const auto tag = juce::Rectangle<int>(r.getRight() - 110, r.getY() + 8, 100, 22);
        g.setColour(accent);
        g.fillRoundedRectangle(tag.toFloat(), 3.0f);
        g.setColour(juce::Colours::white);
        g.setFont(Fonts::get().semibold(12.0f));
        g.drawText(juce::String::charToString((juce::juce_wchar) 0x25CF) + " REC VIDEO", tag, juce::Justification::centred, false);
    }
}

void CameraView::refresh()
{
    if (camera.isRecording() != recordingShown)
    {
        recordingShown = camera.isRecording();
        cameraBox.setEnabled(!recordingShown);
        repaint();
    }
}

CameraWindow::CameraWindow(VideoCapture& c, PlayerRecorder& p, std::function<void()> onClosed)
    : juce::DocumentWindow("AMPSURD - Camera", background, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      closed(std::move(onClosed))
{
    setLookAndFeel(&lnf);
    auto v = std::make_unique<CameraView>(c, p);
    contentView = v.get();
    v->setSize(560, 470);
    setContentOwned(v.release(), true);
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(360, 320, 1920, 1400);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}
CameraWindow::~CameraWindow()
{
    clearContentComponent();
    setLookAndFeel(nullptr);
}
} // namespace ampsurd::ui
