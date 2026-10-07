#pragma once

#include <array>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/Components.h"
#include "ui/Theme.h"

// AMPSURD main window: header / five fixed amp slots / edit area / master / branding footer.
// Laid out at a fixed logical size (kWidth x kHeight) and scaled as a whole when resized.
class AmpsurdEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 1200, kHeight = 800;

    explicit AmpsurdEditor(AmpsurdProcessor&);
    ~AmpsurdEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // for tests / screenshots
    void selectSlot(int slot);
    void refreshAll() { timerCallback(); }

private:
    void timerCallback() override;

    struct Content final : public juce::Component
    {
        explicit Content(AmpsurdProcessor&);
        void paint(juce::Graphics&) override;
        void resized() override;

        ampsurd::ui::HeaderBar header;
        std::array<std::unique_ptr<ampsurd::ui::SlotComponent>, AmpsurdProcessor::kNumSlots> slots;
        ampsurd::ui::EditPanel edit;
        ampsurd::ui::CentrePanel centre;
        ampsurd::ui::MasterPanel master;
        ampsurd::ui::BrandingFooter footer;
    };

    AmpsurdProcessor& proc;
    ampsurd::ui::LookAndFeel lnf;
    Content content;
    juce::TooltipWindow tooltips { this, 600 };
    int selected = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpsurdEditor)
};
