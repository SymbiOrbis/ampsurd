// AMPSURD standalone application (Windows app; built with the plugin's shared code).
//
// Based on JUCE's default standalone app, with guitar-friendly defaults:
//   - the audio input is NOT muted on first start (JUCE mutes it by default to avoid feedback
//     with built-in microphones; AMPSURD is played with a guitar through an audio interface).
// The player / recorder lives in the plugin's own UI and is only shown in the standalone app.

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

    void initialise(const juce::String&) override
    {
        auto* settings = appProperties.getUserSettings();
        const bool firstStart = !settings->containsKey("shouldMuteInput");
        auto holder = std::make_unique<juce::StandalonePluginHolder>(settings, false, juce::String {}, nullptr,
                                                                     juce::Array<juce::StandalonePluginHolder::PluginInOuts> {}, false);
        if (firstStart)
            holder->getMuteInputValue() = false; // guitar through an audio interface: hear it right away

        // the recorder lines takes up with the backing using the audio device's own latency
        if (auto* p = dynamic_cast<AmpsurdProcessor*>(holder->processor.get()))
            if (auto* pl = p->getPlayer())
                pl->setDeviceLatencyProvider([h = holder.get()] {
                    if (auto* d = h->deviceManager.getCurrentAudioDevice())
                        return d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
                    return 0;
                });

        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            headless = std::move(holder);
            return;
        }
        mainWindow = std::make_unique<juce::StandaloneFilterWindow>(getApplicationName(), juce::Colour(0xff1d1e20), std::move(holder));
        mainWindow->setVisible(true);
    }

    void shutdown() override
    {
        headless = nullptr;
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
    std::unique_ptr<juce::StandalonePluginHolder> headless;
};
} // namespace ampsurd

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new ampsurd::StandaloneApp(); }

#endif
