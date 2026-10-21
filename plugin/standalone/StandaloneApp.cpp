// AMPSURD standalone application (Windows and macOS; built with the plugin's shared code).
//
// Based on JUCE's default standalone app, with guitar-friendly defaults:
//   - the audio input is NOT muted on first start (JUCE mutes it by default to avoid feedback
//     with built-in microphones; AMPSURD is played with a guitar through an audio interface).
//   - its own Audio / MIDI settings window: GUITAR INPUT (any input of the interface, by name) on
//     top and the inputs listed one by one (JUCE's window shows them only as pairs "Input 1 + 2", so
//     the guitar jack of e.g. a Scarlett Solo - input 2 - could not be chosen there).
// The player / recorder lives in the plugin's own UI and is only shown in the standalone app.

#include <functional>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#if JucePlugin_Build_Standalone

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "../PluginProcessor.h"

namespace ampsurd
{
namespace
{
// which device input feeds processor channel `rank` (active inputs are packed in order)
int deviceInputForRank(juce::AudioDeviceManager& dm, int rank)
{
    auto* d = dm.getCurrentAudioDevice();
    if (d == nullptr) return -1;
    const auto active = d->getActiveInputChannels();
    int seen = 0;
    for (int ch = 0; ch <= active.getHighestBit(); ++ch)
        if (active[ch] && seen++ == rank) return ch;
    return -1;
}

// the guitar comes from device input `ch` alone (processor channel 1 = input 1 of AMPSURD)
void useDeviceInputForGuitar(juce::StandalonePluginHolder& h, AmpsurdProcessor& p, int ch)
{
    auto setup = h.deviceManager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setBit(ch);
    h.deviceManager.setAudioDeviceSetup(setup, true);
    p.setGuitarInput(0);
    h.saveAudioDeviceState();
}

class AudioSettings final : public juce::Component, private juce::ChangeListener
{
public:
    AudioSettings(juce::StandalonePluginHolder& h, AmpsurdProcessor& p)
        : holder(h), proc(p),
          selector(h.deviceManager, 0, 2, 0, 2, true, true, false /* inputs one by one */, false)
    {
        setOpaque(true);
        guitarLabel.setText("Guitar input:", juce::dontSendNotification);
        guitarLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(guitarLabel);
        addAndMakeVisible(guitarBox);
        guitarBox.setTooltip("The input of your audio interface the guitar is plugged into (e.g. Scarlett Solo: the INST jack is input 2)");
        guitarBox.onChange = [this] {
            const int ch = guitarBox.getSelectedId() - 1;
            if (ch >= 0 && ch != deviceInputForRank(holder.deviceManager, proc.getGuitarInput()))
                useDeviceInputForGuitar(holder, proc, ch);
        };
        muteButton.setButtonText("Mute audio input (protection against feedback with a built-in microphone)");
        muteButton.getToggleStateValue().referTo(holder.getMuteInputValue());
        addAndMakeVisible(muteButton);
        addAndMakeVisible(selector);
        holder.deviceManager.addChangeListener(this);
        updateGuitarBox();
        setSize(560, 600);
    }
    ~AudioSettings() override
    {
        holder.deviceManager.removeChangeListener(this);
        holder.saveAudioDeviceState();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(12, 10);
        auto top = r.removeFromTop(28);
        guitarLabel.setBounds(top.removeFromLeft(150));
        top.removeFromLeft(8);
        guitarBox.setBounds(top.removeFromLeft(260));
        r.removeFromTop(6);
        muteButton.setBounds(r.removeFromTop(24).withTrimmedLeft(158));
        r.removeFromTop(8);
        selector.setBounds(r);
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { updateGuitarBox(); }
    void updateGuitarBox()
    {
        guitarBox.clear(juce::dontSendNotification);
        if (auto* d = holder.deviceManager.getCurrentAudioDevice())
        {
            const auto names = d->getInputChannelNames();
            for (int i = 0; i < names.size(); ++i)
                guitarBox.addItem(names[i], i + 1);
            const int cur = deviceInputForRank(holder.deviceManager, proc.getGuitarInput());
            guitarBox.setSelectedId(cur + 1, juce::dontSendNotification);
        }
    }

    juce::StandalonePluginHolder& holder;
    AmpsurdProcessor& proc;
    juce::Label guitarLabel;
    juce::ComboBox guitarBox;
    juce::ToggleButton muteButton;
    juce::AudioDeviceSelectorComponent selector;
};

void showAudioSettings(juce::StandalonePluginHolder& h, AmpsurdProcessor& p)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(new AudioSettings(h, p));
    o.dialogTitle = "Audio / MIDI Settings";
    o.dialogBackgroundColour = o.content->getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}
} // namespace

// covers JUCE's Options button and takes its clicks (follows the button's size)
struct ClickCatcher final : public juce::Component
{
    std::function<void()> onDown;
    void parentSizeChanged() override { if (auto* p = getParentComponent()) setBounds(p->getLocalBounds()); }
    void mouseDown(const juce::MouseEvent&) override { if (onDown) onDown(); }
};

class StandaloneApp final : public juce::JUCEApplication
{
public:
    StandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = JucePlugin_Name;
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
       #else
        options.folderName = "AMPSURD";
       #endif
        appProperties.setStorageParameters(options);
    }

    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted(const juce::String&) override {}

    // links the processor to the audio device (again after "Reset to default state" re-creates it)
    static void connectProcessor(juce::StandalonePluginHolder& holderRef)
    {
        auto* h = &holderRef;
        // the recorder lines takes up with the backing using the audio device's own latency
        if (auto* p = dynamic_cast<AmpsurdProcessor*>(h->processor.get()))
            if (auto* pl = p->getPlayer())
                pl->setDeviceLatencyProvider([h] {
                    if (auto* d = h->deviceManager.getCurrentAudioDevice())
                        return d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
                    return 0;
                });

        // guitar input by device input + the app's own Audio / MIDI settings window (inputs one by one)
        if (auto* p = dynamic_cast<AmpsurdProcessor*>(h->processor.get()))
        {
            AmpsurdProcessor::StandaloneAudio sa;
            sa.inputNames = [h] {
                auto* d = h->deviceManager.getCurrentAudioDevice();
                return d != nullptr ? d->getInputChannelNames() : juce::StringArray {};
            };
            sa.guitarDeviceInput = [h, p] { return deviceInputForRank(h->deviceManager, p->getGuitarInput()); };
            sa.setGuitarDeviceInput = [h, p](int ch) { useDeviceInputForGuitar(*h, *p, ch); };
            sa.showAudioSettings = [h, p] { showAudioSettings(*h, *p); };
            p->setStandaloneAudio(std::move(sa));
        }

        // dropout counter in the footer: the driver's missed buffers (ASIO / WASAPI report them)
        if (auto* p = dynamic_cast<AmpsurdProcessor*>(h->processor.get()))
            p->setDeviceXRunProvider([h] { return h->deviceManager.getXRunCount(); });
    }

    void initialise(const juce::String&) override
    {
        auto* settings = appProperties.getUserSettings();
        const bool firstStart = !settings->containsKey("shouldMuteInput");
        auto holder = std::make_unique<juce::StandalonePluginHolder>(settings, false, juce::String {}, nullptr,
                                                                     juce::Array<juce::StandalonePluginHolder::PluginInOuts> {}, false);
        if (firstStart)
            holder->getMuteInputValue() = false; // guitar through an audio interface: hear it right away

        connectProcessor(*holder);

        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            headless = std::move(holder);
            return;
        }
        mainWindow = std::make_unique<juce::StandaloneFilterWindow>(getApplicationName(), juce::Colour(0xff1d1e20), std::move(holder));
        // the window's Options menu opens AMPSURD's Audio / MIDI settings instead of JUCE's (same items
        // otherwise): a transparent catcher on top of JUCE's Options button takes its clicks
        for (auto* c : mainWindow->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->getButtonText() == "Options")
            {
                b->setInterceptsMouseClicks(false, true);
                optionsCatcher = std::make_unique<ClickCatcher>();
                optionsCatcher->onDown = [this, b] {
                    juce::PopupMenu m;
                    m.addItem(1, "Audio / MIDI Settings...");
                    m.addSeparator();
                    m.addItem(2, "Save current state...");
                    m.addItem(3, "Load a saved state...");
                    m.addSeparator();
                    m.addItem(4, "Reset to default state");
                    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(b), [this](int r) {
                        if (mainWindow == nullptr) return;
                        auto* h = mainWindow->pluginHolder.get();
                        if (r == 1)
                        {
                            if (auto* p = dynamic_cast<AmpsurdProcessor*>(h->processor.get())) showAudioSettings(*h, *p);
                            else h->showAudioSettingsDialog();
                        }
                        if (r == 2) h->askUserToSaveState();
                        if (r == 3) h->askUserToLoadState();
                        if (r == 4)
                        {
                            mainWindow->resetToDefaultState();
                            connectProcessor(*mainWindow->pluginHolder);
                        }
                    });
                };
                b->addAndMakeVisible(*optionsCatcher);
                optionsCatcher->setBounds(b->getLocalBounds());
            }
        mainWindow->setVisible(true);
    }

    void shutdown() override
    {
        headless = nullptr;
        optionsCatcher = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (headless != nullptr) headless->savePluginState();
        if (mainWindow != nullptr) mainWindow->pluginHolder->savePluginState();
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            juce::Timer::callAfterDelay(100, [] {
                if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
            });
        else
            quit();
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
    std::unique_ptr<ClickCatcher> optionsCatcher;
    std::unique_ptr<juce::StandalonePluginHolder> headless;
};
} // namespace ampsurd

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new ampsurd::StandaloneApp(); }

#endif
