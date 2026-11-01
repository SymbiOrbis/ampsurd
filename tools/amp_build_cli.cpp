// Developer tool: builds an .ampsurdamp from a folder of recordings, like AMPSURD's settings menu ->
// "Build an AMPSURD amp from recordings..." (same code: plugin/AmpProfileFile.cpp), without the GUI.
//   ampsurd_amp_build <folder> <out.ampsurdamp> "<name>" ["<author>"]
#include <iostream>
#include <juce_core/juce_core.h>
#include "AmpProfileFile.h"

int main(int argc, char** argv)
{
    if (argc < 4) { std::cerr << "usage: ampsurd_amp_build <folder> <out.ampsurdamp> <name> [author]\n"; return 2; }
    juce::String report, error;
    const bool ok = ampsurd::amp::build(juce::File(juce::String::fromUTF8(argv[1])), juce::String::fromUTF8(argv[3]),
                                        argc > 4 ? juce::String::fromUTF8(argv[4]) : juce::String(), juce::File(juce::String::fromUTF8(argv[2])),
                                        report, error);
    std::cout << report;
    if (!ok) { std::cerr << "ERROR: " << error << "\n"; return 1; }
    std::cout << "written: " << argv[2] << "\n";
    return 0;
}
