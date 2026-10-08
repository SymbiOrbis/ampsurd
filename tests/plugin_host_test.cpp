// End-to-end test of the built VST3: loads the real plugin binary through JUCE's VST3 host,
// restores a rig through the plugin state (exactly what a DAW does on project load),
// renders a WAV and reports latency, loudness and peak.
//
// Usage: plugin_host_test <AMPSURD.vst3> <in.wav> <out.wav> <block> <levelMatch 0|1> <outputGainDb> <a.nam> [b.nam ...]

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>

static juce::MemoryBlock decodeInner(juce::XmlElement& outer)
{
    juce::MemoryBlock inner;
    if (auto* comp = outer.getChildByName("IComponent"))
        inner.fromBase64Encoding(comp->getAllSubText());
    return inner;
}

int main(int argc, char** argv)
{
    if (argc < 8)
    {
        std::cerr << "Usage: plugin_host_test <plugin.vst3> <in.wav> <out.wav> <block> <levelMatch> <outDb> <a.nam> [...]\n";
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const int block = std::atoi(argv[4]);
    const bool levelMatch = std::atoi(argv[5]) != 0;
    const float outDb = (float) std::atof(argv[6]);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(argv[2])));
    if (!reader) { std::cerr << "cannot read input\n"; return 1; }
    const double sr = reader->sampleRate;
    juce::AudioBuffer<float> input(1, (int) reader->lengthInSamples);
    reader->read(&input, 0, input.getNumSamples(), 0, true, false);

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descs;
    vst3.findAllTypesForFile(descs, argv[1]);
    if (descs.isEmpty()) { std::cerr << "no plugin found\n"; return 2; }
    std::cout << "Found plugin: " << descs[0]->name << " by " << descs[0]->manufacturerName << "\n";

    juce::String err;
    auto plugin = vst3.createInstanceFromDescription(*descs[0], sr, block, err);
    if (!plugin) { std::cerr << "instantiate failed: " << err << "\n"; return 3; }
    plugin->setPlayConfigDetails(2, 2, sr, block);
    plugin->prepareToPlay(sr, block);
    std::cout << "Parameters exposed to the host: " << plugin->getParameters().size() << "\n";

    // Build the rig through the plugin's own state format.
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    auto outer = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
    if (!outer) { std::cerr << "unexpected host state\n"; return 4; }
    auto innerData = decodeInner(*outer);
    auto xml = juce::AudioProcessor::getXmlFromBinary(innerData.getData(), (int) innerData.getSize());
    if (!xml) { std::cerr << "plugin state is not XML\n"; return 4; }
    const int numCaps = juce::jmin(argc - 7, 5);
    if (auto* slots = xml->getChildByName("SLOTS"))
        for (auto* s : slots->getChildIterator())
        {
            const int idx = s->getIntAttribute("index") - 1;
            s->setAttribute("path", idx < numCaps ? juce::File(argv[7 + idx]).getFullPathName() : juce::String());
            // AMPSURD_TEST_IR=<file>: cabinet IR for slot 1
            const auto irPath = juce::SystemStats::getEnvironmentVariable("AMPSURD_TEST_IR", "");
            if (idx == 0 && irPath.isNotEmpty()) s->setAttribute("irPath", juce::File(irPath).getFullPathName());
        }
    for (auto* p : xml->getChildIterator())
        if (p->hasTagName("PARAM"))
        {
            const auto id = p->getStringAttribute("id");
            if (id == "levelMatch") p->setAttribute("value", levelMatch ? 1.0 : 0.0);
            if (id == "output") p->setAttribute("value", outDb);
            if (id == "gateOn" && juce::SystemStats::getEnvironmentVariable("AMPSURD_TEST_GATE_OFF", "0") == "1")
                p->setAttribute("value", 0.0);
            if (id == "bypass" && juce::SystemStats::getEnvironmentVariable("AMPSURD_TEST_BYPASS", "0") == "1")
                p->setAttribute("value", 1.0);
            for (int i = 0; i < 5; ++i)
                if (id == "s" + juce::String(i + 1) + "_mix") p->setAttribute("value", i < numCaps ? 100.0 / numCaps : 0.0);
            // AMPSURD_TEST_PARAMS="id=value,id=value": any parameter
            for (auto kv : juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("AMPSURD_TEST_PARAMS", ""), ",", ""))
                if (kv.upToFirstOccurrenceOf("=", false, false).trim() == id)
                    p->setAttribute("value", kv.fromFirstOccurrenceOf("=", false, false).getDoubleValue());
            // AMPSURD_TEST_PAN="-100,100,..." sets the PAN of slots 1, 2, ...
            const auto pans = juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("AMPSURD_TEST_PAN", ""), ",", "");
            for (int i = 0; i < pans.size() && i < 5; ++i)
                if (pans[i].isNotEmpty() && id == "s" + juce::String(i + 1) + "_pan") p->setAttribute("value", pans[i].getDoubleValue());
        }
    juce::MemoryBlock newInner;
    juce::AudioProcessor::copyXmlToBinary(*xml, newInner);
    if (auto* comp = outer->getChildByName("IComponent"))
    {
        comp->deleteAllTextElements();
        comp->addTextElement(newInner.toBase64Encoding());
    }
    juce::MemoryBlock newState;
    juce::AudioProcessor::copyXmlToBinary(*outer, newState);
    plugin->setStateInformation(newState.getData(), (int) newState.getSize());

    // Captures load + get measured on the plugin's background thread; keep "audio" running.
    juce::AudioBuffer<float> io(2, block);
    juce::MidiBuffer midi;
    for (int i = 0; i < 400; ++i)
    {
        io.clear();
        plugin->processBlock(io, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10); // like a DAW: message thread keeps running
    }
    std::cout << "Latency reported to host: " << plugin->getLatencySamples() << " samples ("
              << juce::String(plugin->getLatencySamples() * 1000.0 / sr, 2) << " ms)\n";

    juce::AudioBuffer<float> output(2, input.getNumSamples()); // stereo out (per-amp PAN)
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < input.getNumSamples(); pos += block)
    {
        const int n = juce::jmin(block, input.getNumSamples() - pos);
        juce::AudioBuffer<float> chunk(2, n);
        chunk.copyFrom(0, 0, input, 0, pos, n);
        chunk.copyFrom(1, 0, input, 0, pos, n);
        plugin->processBlock(chunk, midi);
        output.copyFrom(0, pos, chunk, 0, 0, n);
        output.copyFrom(1, pos, chunk, 1, 0, n);
    }
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::cout << "Rendered " << input.getNumSamples() / sr << " s in " << ms << " ms ("
              << juce::String(100.0 * ms / 1000.0 / (input.getNumSamples() / sr), 1) << " % of real time)\n";
    std::cout << "Output peak: " << juce::Decibels::gainToDecibels(output.getMagnitude(0, output.getNumSamples()), -200.0f) << " dBFS\n";

    // Saved state must reference the captures.
    plugin->getStateInformation(state);
    if (auto o = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize()))
    {
        auto in2 = decodeInner(*o);
        if (auto saved = juce::AudioProcessor::getXmlFromBinary(in2.getData(), (int) in2.getSize()))
            if (auto* slots = saved->getChildByName("SLOTS"))
            {
                int referenced = 0;
                for (auto* s : slots->getChildIterator()) referenced += s->getStringAttribute("path").isNotEmpty();
                std::cout << "Saved state references " << referenced << " capture file(s)\n";
            }
    }

    juce::File outFile(argv[3]);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os(outFile.createOutputStream().release());
    auto writer = wav.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(sr).withNumChannels(2).withBitsPerSample(32)
                                              .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (!writer) { std::cerr << "cannot create writer\n"; return 5; }
    writer->writeFromAudioSampleBuffer(output, 0, output.getNumSamples());
    writer.reset();

    plugin->releaseResources();
    plugin.reset();
    return 0;
}
