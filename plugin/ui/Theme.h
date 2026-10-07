#pragma once

// AMPSURD visual language (docs/UI_SPEC.md): flat neutral dark grey, light grey/white text,
// thin precise lines, no gradients, no colour-coded state. State is always shown by fill,
// text or line weight - never by colour alone.

#include <juce_gui_basics/juce_gui_basics.h>

namespace ampsurd::ui
{

namespace colours
{
inline const juce::Colour background { 0xff1d1e20 };
inline const juce::Colour panel      { 0xff1d1e20 }; // same as background: panels are outlined, not filled
inline const juce::Colour raised     { 0xff242528 }; // subtle fill: selected slot, hovered controls
inline const juce::Colour line       { 0xff3a3c40 }; // thin borders
inline const juce::Colour lineStrong { 0xff8d8f93 }; // selected border
inline const juce::Colour grid       { 0xff2b2d30 };
inline const juce::Colour text       { 0xffe8e8e6 };
inline const juce::Colour textDim    { 0xffb9bbbe }; // secondary text, labels, scales (contrast ~9:1)
inline const juce::Colour textFaint  { 0xff8f9195 }; // least important text, still readable (contrast ~5.5:1)
inline const juce::Colour onFill     { 0xffe8e8e6 }; // "on" buttons: light fill, dark text
inline const juce::Colour onText     { 0xff1d1e20 };
inline const juce::Colour warning    { 0xffe8e8e6 }; // warnings are shown with text, not colour
} // namespace colours

// Embedded Inter (SIL Open Font License 1.1), so text and filename wrapping look the same on
// every computer.
struct Fonts
{
    static const Fonts& get();
    juce::Font regular(float size) const;
    juce::Font medium(float size) const;
    juce::Font semibold(float size) const;
    // UPPERCASE control labels with a little extra tracking
    juce::Font label(float size = 11.0f) const;

private:
    Fonts();
    juce::Typeface::Ptr reg, med, semi;
};

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;

    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
};

} // namespace ampsurd::ui
