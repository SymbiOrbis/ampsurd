// End-to-end test of the built VST3: loads the real plugin binary through JUCE's VST3
// host, points it at a capture via the saved-state mechanism (exactly what a DAW does on
// project load), renders a WAV and writes the result for comparison with monstrosity_render.
//
// Usage: plugin_host_test <MONSTROSITY.vst3> <model.nam> <input.wav> <output.wav> [block]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::cerr << "Usage: plugin_host_test <plugin.vst3> <model.nam> <input.wav> <output.wav> [block]\n";
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const int block = argc > 5 ? std::atoi(argv[5]) : 64;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(argv[3])));
    if (!reader) { std::cerr << "cannot read input\n"; return 1; }
    const double sr = reader->sampleRate;
    juce::AudioBuffer<float> input(1, (int) reader->lengthInSamples);
    reader->read(&input, 0, input.getNumSamples(), 0, true, false);

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descs;
    vst3.findAllTypesForFile(descs, argv[1]);
    if (descs.isEmpty()) { std::cerr << "no plugin found in " << argv[1] << "\n"; return 2; }
    std::cout << "Found plugin: " << descs[0]->name << " by " << descs[0]->manufacturerName << "\n";

    juce::String err;
    auto plugin = vst3.createInstanceFromDescription(*descs[0], sr, block, err);
    if (!plugin) { std::cerr << "instantiate failed: " << err << "\n"; return 3; }

    plugin->setPlayConfigDetails(2, 2, sr, block);
    plugin->prepareToPlay(sr, block);

    // Inject the capture path into the plugin's own state, as a DAW project would.
    // JUCE's VST3 host wraps the plugin's state: <VST3PluginState><IComponent>base64</IComponent>.
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    auto outer = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
    auto* comp = outer ? outer->getChildByName("IComponent") : nullptr;
    if (!comp) { std::cerr << "unexpected host state format\n"; return 4; }
    juce::MemoryBlock inner;
    inner.fromBase64Encoding(comp->getAllSubText());
    auto xml = juce::AudioProcessor::getXmlFromBinary(inner.getData(), (int) inner.getSize());
    if (!xml) { std::cerr << "plugin state is not XML\n"; return 4; }
    xml->setAttribute("capturePath", juce::File(argv[2]).getFullPathName());
    juce::MemoryBlock newInner;
    juce::AudioProcessor::copyXmlToBinary(*xml, newInner);
    comp->deleteAllTextElements();
    comp->addTextElement(newInner.toBase64Encoding());
    juce::MemoryBlock newState;
    juce::AudioProcessor::copyXmlToBinary(*outer, newState);
    plugin->setStateInformation(newState.getData(), (int) newState.getSize());

    // The capture loads on the plugin's background thread; give it time, keep "audio" running.
    juce::AudioBuffer<float> io(2, block);
    juce::MidiBuffer midi;
    for (int i = 0; i < 300; ++i)
    {
        io.clear();
        plugin->processBlock(io, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    std::cout << "Latency reported to host: " << plugin->getLatencySamples() << " samples\n";

    juce::AudioBuffer<float> output(1, input.getNumSamples());
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < input.getNumSamples(); pos += block)
    {
        const int n = juce::jmin(block, input.getNumSamples() - pos);
        juce::AudioBuffer<float> chunk(2, n);
        chunk.copyFrom(0, 0, input, 0, pos, n);
        chunk.copyFrom(1, 0, input, 0, pos, n);
        plugin->processBlock(chunk, midi);
        output.copyFrom(0, pos, chunk, 0, 0, n);
    }
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::cout << "Rendered " << input.getNumSamples() / sr << " s in " << ms << " ms\n";

    plugin->getStateInformation(state);
    if (auto o = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize()))
        if (auto* c = o->getChildByName("IComponent"))
        {
            juce::MemoryBlock in2;
            in2.fromBase64Encoding(c->getAllSubText());
            if (auto saved = juce::AudioProcessor::getXmlFromBinary(in2.getData(), (int) in2.getSize()))
                std::cout << "Saved state capturePath: " << saved->getStringAttribute("capturePath") << "\n";
        }

    juce::File outFile(argv[4]);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os(outFile.createOutputStream().release());
    auto writer = wav.createWriterFor(os, juce::AudioFormatWriterOptions{}.withSampleRate(sr).withNumChannels(1).withBitsPerSample(32)
                                                .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (!writer) { std::cerr << "cannot create writer\n"; return 5; }
    writer->writeFromAudioSampleBuffer(output, 0, output.getNumSamples());
    writer.reset();

    plugin->releaseResources();
    plugin.reset();
    return 0;
}
