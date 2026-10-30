#pragma once

// AMPSURD amp (.ampsurdamp, owner 2026-10-30): one amp with real knobs.
//
//   a zip file:  amp.xml           name, author, which capture belongs to which GAIN position, the tone controls
//                <captures>.nam    standard NAM captures (one per GAIN position, tone knobs at noon) - they also
//                                  work alone in any NAM player
//
// AMPSURD morphs between the gain captures (core: CaptureModel::makeMorph) and applies the measured BASS / MID /
// TREBLE curves as a filter (core: ToneStack). The builder makes the file from the owner's recordings, named as
// in the recording sheet: gain captures "...gain_HHMM....nam" (HHMM = the knob's clock position) and tone sweeps
// "Tnn_ref..." / "Tnn_bass_min" / "Tnn_mid_0900" / "Tnn_treble_1500" / "..._max" (.wav, 48 kHz, the whole
// AMPSURD_tone_sweep_48k.wav recorded through the amp, gain at minimum).

#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "ampsurd/AmpProfile.h"

namespace ampsurd::amp
{
struct Profile
{
    juce::String name, author;
    struct Step { double knob = 5.0; juce::File nam; };
    std::vector<Step> steps;                       // ascending GAIN; files unpacked into a cache folder
    std::shared_ptr<const ToneStack> tone;         // may be empty (no tone sweeps): no BASS / MID / TREBLE
};

// Reads an .ampsurdamp (unpacks its captures once into AMPSURD's cache folder). false + error if unusable.
bool read(const juce::File& file, Profile& out, juce::String& error);

// Clock position "0900" / "1500" / "0300" (= 3 pm) / "1230" -> knob 0..10 (7:00 = 0, noon = 5, 5:00 = 10).
// "min" -> 0, "max" -> 10. -1 = not a position.
double knobFromText(const juce::String& t);

// Builds an .ampsurdamp from a folder of recordings (see above). `report`: what was found / used, per sweep whether
// the quiet sweep had to be used, the drift between the two all-noon references.
bool build(const juce::File& folder, const juce::String& name, const juce::String& author, const juce::File& out,
           juce::String& report, juce::String& error);
} // namespace ampsurd::amp
