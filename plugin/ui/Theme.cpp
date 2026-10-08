#include "Theme.h"

#include "BinaryData.h"

namespace ampsurd::ui
{

namespace
{
juce::Typeface::Ptr loadFont(const char* originalFileName)
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (juce::String(BinaryData::originalFilenames[i]) == originalFileName)
        {
            int size = 0;
            if (const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size))
                return juce::Typeface::createSystemTypefaceFor(data, (size_t) size);
        }
    return nullptr;
}
} // namespace

Fonts::Fonts()
    : reg(loadFont("Inter-Regular.ttf")), med(loadFont("Inter-Medium.ttf")), semi(loadFont("Inter-SemiBold.ttf"))
{
}

const Fonts& Fonts::get()
{
    static const Fonts f;
    return f;
}

static juce::Font make(const juce::Typeface::Ptr& t, float size)
{
    juce::FontOptions o;
    if (t != nullptr) o = o.withTypeface(t);
    return juce::Font(o.withHeight(size));
}

juce::Font Fonts::regular(float s) const { return make(reg, s); }
juce::Font Fonts::medium(float s) const { return make(med, s); }
juce::Font Fonts::semibold(float s) const { return make(semi, s); }
juce::Font Fonts::label(float s) const { return make(semi, s).withExtraKerningFactor(0.09f); }

// ---------------------------------------------------------------------------------------------
LookAndFeel::LookAndFeel()
{
    using namespace colours;
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::TextButton::buttonColourId, background);
    setColour(juce::TextButton::buttonOnColourId, onFill);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, onText);
    setColour(juce::Label::textColourId, text);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::PopupMenu::backgroundColourId, raised);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, line);
    setColour(juce::PopupMenu::highlightedTextColourId, text);
    setColour(juce::TooltipWindow::backgroundColourId, raised);
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, line);
    setColour(juce::AlertWindow::backgroundColourId, raised);
    setColour(juce::AlertWindow::textColourId, text);
    setColour(juce::AlertWindow::outlineColourId, line);
    setColour(juce::TextEditor::backgroundColourId, background);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::outlineColourId, line);
    setColour(juce::TextEditor::focusedOutlineColourId, lineStrong);
    setColour(juce::CaretComponent::caretColourId, text);
    setColour(juce::ComboBox::backgroundColourId, background);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, line);
    setColour(juce::ComboBox::arrowColourId, textDim);
}

// drop-down boxes: same flat outline style as the buttons, small caret
void LookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace colours;
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h).reduced(0.5f);
    const float a = box.isEnabled() ? 1.0f : 0.4f;
    if (box.isMouseOver(true) && box.isEnabled())
    {
        g.setColour(raised);
        g.fillRoundedRectangle(r, 2.0f);
    }
    g.setColour((box.isMouseOver(true) && box.isEnabled() ? lineStrong : line).withAlpha(a));
    g.drawRoundedRectangle(r, 2.0f, 1.0f);
    juce::Path caret;
    const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
    caret.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour(textDim.withAlpha(a));
    g.fillPath(caret);
}

juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&) { return Fonts::get().medium(12.5f); }

void LookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(4, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont(const juce::Font& f)
{
    const auto& fonts = Fonts::get();
    if (f.getTypefacePtr() != nullptr && f.getTypefaceName() != juce::Font::getDefaultSansSerifFontName())
        return f.getTypefacePtr();
    if (auto t = fonts.regular(f.getHeight()).getTypefacePtr())
        return t;
    return LookAndFeel_V4::getTypefaceForFont(f);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    using namespace colours;
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    if (on)
    {
        g.setColour(onFill.withAlpha(b.isEnabled() ? 1.0f : 0.3f));
        g.fillRoundedRectangle(r, 2.0f);
        return;
    }
    if ((bool) b.getProperties().getWithDefault("lit", false))
    {
        // "lit": something is active here (same look as the GLOBAL EQ button when on)
        g.setColour(text.withAlpha((highlighted || down) ? 0.20f : 0.14f));
        g.fillRoundedRectangle(r, 2.0f);
        g.setColour(lineStrong.withAlpha(b.isEnabled() ? 1.0f : 0.5f));
        g.drawRoundedRectangle(r, 2.0f, 1.2f);
        return;
    }

    if ((highlighted || down) && b.isEnabled())
    {
        g.setColour(raised);
        g.fillRoundedRectangle(r, 2.0f);
    }
    g.setColour((highlighted && b.isEnabled() ? lineStrong : line).withAlpha(b.isEnabled() ? 1.0f : 0.5f));
    g.drawRoundedRectangle(r, 2.0f, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return Fonts::get().label(11.0f);
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    using namespace colours;
    const bool on = b.getToggleState();
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour((on ? onText : text).withAlpha(b.isEnabled() ? 1.0f : 0.35f));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(3, 0), juce::Justification::centred, 1, 0.85f);
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                   juce::Slider::SliderStyle style, juce::Slider& s)
{
    using namespace colours;
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    if (style == juce::Slider::LinearHorizontal)
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour(line.withAlpha(alpha));
        g.fillRect((float) x, cy - 0.5f, (float) w, 1.0f);
        // value bar from the slider's "zero" (centre for bipolar ranges)
        const double zero = s.getMinimum() < 0.0 && s.getMaximum() > 0.0 ? 0.0 : s.getMinimum();
        const float zx = (float) s.getPositionOfValue(zero);
        g.setColour(textDim.withAlpha(alpha));
        g.fillRect(juce::jmin(zx, pos), cy - 1.0f, std::abs(pos - zx), 2.0f);
        g.setColour(text.withAlpha(alpha));
        g.fillRoundedRectangle(pos - 3.0f, cy - 7.0f, 6.0f, 14.0f, 1.5f);
    }
    else
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s);
    }
}

juce::Slider::SliderLayout LookAndFeel::getSliderLayout(juce::Slider& s)
{
    juce::Slider::SliderLayout l;
    auto r = s.getLocalBounds();
    if (s.getTextBoxPosition() == juce::Slider::TextBoxRight)
    {
        l.textBoxBounds = r.removeFromRight(s.getTextBoxWidth());
        r.removeFromRight(8);
    }
    l.sliderBounds = r.reduced(4, 0);
    return l;
}

juce::Label* LookAndFeel::createSliderTextBox(juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(Fonts::get().medium(12.0f));
    l->setJustificationType(juce::Justification::centredRight);
    l->setColour(juce::Label::textColourId, colours::text);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::outlineWhenEditingColourId, colours::lineStrong);
    l->setColour(juce::TextEditor::backgroundColourId, colours::raised);
    return l;
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(colours::raised);
    g.setColour(colours::line);
    g.drawRect(0, 0, w, h, 1);
}

void LookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                    bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                    const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    using namespace colours;
    if (isSeparator)
    {
        g.setColour(line);
        g.fillRect(area.reduced(8, 0).withHeight(1).withY(area.getCentreY()));
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour(line);
        g.fillRect(area);
    }
    auto r = area.reduced(12, 0);
    g.setColour((isActive ? colours::text : textFaint));
    g.setFont(getPopupMenuFont());
    if (isTicked)
        g.fillEllipse((float) r.getX(), (float) area.getCentreY() - 2.5f, 5.0f, 5.0f);
    r.removeFromLeft(14);
    g.drawText(text, r, juce::Justification::centredLeft, true);
    if (hasSubMenu)
        g.drawText(">", r, juce::Justification::centredRight, false);
}

juce::Font LookAndFeel::getPopupMenuFont() { return Fonts::get().regular(13.0f); }

void LookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll(colours::raised);
    g.setColour(colours::line);
    g.drawRect(0, 0, w, h, 1);
    g.setColour(colours::text);
    g.setFont(Fonts::get().regular(12.5f));
    g.drawFittedText(text, juce::Rectangle<int>(0, 0, w, h).reduced(10, 6), juce::Justification::centredLeft, 12, 1.0f);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto font = Fonts::get().regular(12.5f);
    juce::StringArray lines;
    lines.addLines(tipText);
    int w = 0;
    for (auto& l : lines)
        w = juce::jmax(w, juce::GlyphArrangement::getStringWidthInt(font, l));
    w = juce::jmin(w + 24, 520);
    const int h = (int) (lines.size() * 17) + 14;
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin(parentArea);
}

} // namespace ampsurd::ui
