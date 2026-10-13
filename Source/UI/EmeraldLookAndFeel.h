#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace drumbreaker
{

namespace emerald
{
    const juce::Colour background  { 0xff07100d };
    const juce::Colour panel       { 0xff0d1a16 };
    const juce::Colour panelLight  { 0xff12241e };
    const juce::Colour accentDark  { 0xff0f6b47 };
    const juce::Colour accent      { 0xff1fd692 };
    const juce::Colour accentBright{ 0xff7bffcd };
    const juce::Colour text        { 0xffd8fff0 };
    const juce::Colour textDim     { 0xff6f9e8d };
    const juce::Colour warning     { 0xffffb347 };
}

/** Sleek, glowing emerald styling for every control in the plugin. */
class EmeraldLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EmeraldLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                                           juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;

    /** Soft outer glow used by every hovered control. */
    static void drawGlow (juce::Graphics&, const juce::Path&, juce::Colour, float intensity);

private:
    static juce::TextLayout layoutTooltipText (const juce::String&, juce::Colour);
};

} // namespace drumbreaker
