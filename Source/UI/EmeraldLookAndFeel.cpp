#include "EmeraldLookAndFeel.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace drumbreaker
{

EmeraldLookAndFeel::EmeraldLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, emerald::background);
    setColour (juce::Label::textColourId,                 emerald::text);
    setColour (juce::Slider::textBoxTextColourId,         emerald::text);
    setColour (juce::Slider::textBoxOutlineColourId,      emerald::accent.withAlpha (0.25f));
    setColour (juce::Slider::textBoxBackgroundColourId,   emerald::panel);
    setColour (juce::ComboBox::backgroundColourId,        emerald::panel);
    setColour (juce::ComboBox::textColourId,              emerald::text);
    setColour (juce::ComboBox::outlineColourId,           emerald::accent.withAlpha (0.35f));
    setColour (juce::ComboBox::arrowColourId,             emerald::accent);
    setColour (juce::PopupMenu::backgroundColourId,       emerald::panel);
    setColour (juce::PopupMenu::textColourId,             emerald::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, emerald::accentDark);
    setColour (juce::PopupMenu::highlightedTextColourId,  emerald::accentBright);
    setColour (juce::TextButton::buttonColourId,          emerald::panelLight);
    setColour (juce::TextButton::textColourOffId,         emerald::text);
    setColour (juce::TooltipWindow::backgroundColourId,   emerald::panel);
    setColour (juce::TooltipWindow::textColourId,         emerald::accentBright);
    setColour (juce::TooltipWindow::outlineColourId,      emerald::accent);
    setColour (juce::MidiKeyboardComponent::whiteNoteColourId,        juce::Colour (0xffdff5ec));
    setColour (juce::MidiKeyboardComponent::blackNoteColourId,        juce::Colour (0xff0a1512));
    setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, emerald::accentDark.withAlpha (0.6f));
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, emerald::accent.withAlpha (0.35f));
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,   emerald::accent.withAlpha (0.75f));
    setColour (juce::MidiKeyboardComponent::textLabelColourId,        emerald::accentDark);
}

void EmeraldLookAndFeel::drawGlow (juce::Graphics& g, const juce::Path& path,
                                   juce::Colour colour, float intensity)
{
    if (intensity <= 0.0f)
        return;

    for (int i = 3; i >= 1; --i)
    {
        g.setColour (colour.withAlpha (0.10f * intensity * (float) i / 3.0f));
        g.strokePath (path, juce::PathStrokeType ((float) i * 2.4f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
}

void EmeraldLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                           juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float arcThickness = juce::jmax (2.5f, radius * 0.16f);

    const bool hot = slider.isMouseOverOrDragging() && slider.isEnabled();
    const float glow = hot ? 1.0f : 0.25f;

    // body
    g.setGradientFill (juce::ColourGradient (emerald::panelLight, centre.x, centre.y - radius,
                                             emerald::background, centre.x, centre.y + radius, false));
    g.fillEllipse (bounds.reduced (arcThickness * 1.6f));

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - arcThickness * 0.5f, radius - arcThickness * 0.5f,
                         0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (emerald::accentDark.withAlpha (0.28f));
    g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc - bipolar controls fill outwards from the top
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float zeroAngle = rotaryStartAngle + zeroPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - arcThickness * 0.5f, radius - arcThickness * 0.5f,
                         0.0f, juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);

    drawGlow (g, value, emerald::accent, glow);
    g.setColour (hot ? emerald::accentBright : emerald::accent);
    g.strokePath (value, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    juce::Path pointer;
    const float pointerLength = radius * 0.62f;
    pointer.startNewSubPath (0.0f, -radius * 0.18f);
    pointer.lineTo (0.0f, -pointerLength);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

    drawGlow (g, pointer, emerald::accentBright, glow);
    g.setColour (hot ? juce::Colours::white : emerald::accentBright);
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.8f, radius * 0.10f),
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    // centre cap
    g.setColour (emerald::accent.withAlpha (hot ? 0.9f : 0.5f));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.16f, radius * 0.16f).withCentre (centre));
}

void EmeraldLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float, float,
                                           juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool vertical = style == juce::Slider::LinearVertical;
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const bool hot = slider.isMouseOverOrDragging();

    auto track = vertical ? bounds.withSizeKeepingCentre (5.0f, bounds.getHeight())
                          : bounds.withSizeKeepingCentre (bounds.getWidth(), 5.0f);

    g.setColour (emerald::accentDark.withAlpha (0.3f));
    g.fillRoundedRectangle (track, 2.5f);

    auto filled = track;

    if (vertical)
        filled = filled.withTop (sliderPos);
    else
        filled = filled.withRight (sliderPos);
        if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        {
            const float zeroProportion = (float) slider.valueToProportionOfLength (0.0);
            const float zeroPosition = vertical ? bounds.getBottom() - zeroProportion * bounds.getHeight()
                                                : bounds.getX() + zeroProportion * bounds.getWidth();

            if (vertical)
                filled = filled.withTop (juce::jmin (sliderPos, zeroPosition))
                               .withBottom (juce::jmax (sliderPos, zeroPosition));
            else
                filled = filled.withLeft (juce::jmin (sliderPos, zeroPosition))
                               .withRight (juce::jmax (sliderPos, zeroPosition));
        }
        else if (vertical)
            filled = filled.withTop (sliderPos);
        else
            filled = filled.withRight (sliderPos);

    g.setColour (hot ? emerald::accentBright : emerald::accent);
    g.fillRoundedRectangle (filled, 2.5f);

    juce::Path thumb;
    const auto centre = vertical ? juce::Point<float> (bounds.getCentreX(), sliderPos)
                                 : juce::Point<float> (sliderPos, bounds.getCentreY());
    thumb.addEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (centre));

    drawGlow (g, thumb, emerald::accent, hot ? 1.0f : 0.3f);
    g.setColour (hot ? juce::Colours::white : emerald::accentBright);
    g.fillPath (thumb);
}

void EmeraldLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.5f);
    const float corner = juce::jmin (10.0f, bounds.getHeight() * 0.35f);
    const bool on = button.getToggleState() || shouldDrawButtonAsDown;

    juce::Path shape;
    shape.addRoundedRectangle (bounds, corner);

    g.setGradientFill (juce::ColourGradient (backgroundColour.brighter (on ? 0.35f : 0.12f),
                                             bounds.getCentreX(), bounds.getY(),
                                             backgroundColour.darker (0.4f),
                                             bounds.getCentreX(), bounds.getBottom(), false));
    g.fillPath (shape);

    if (shouldDrawButtonAsHighlighted || on)
        drawGlow (g, shape, emerald::accent, on ? 1.0f : 0.75f);

    g.setColour (emerald::accent.withAlpha (shouldDrawButtonAsHighlighted || on ? 0.95f : 0.45f));
    g.strokePath (shape, juce::PathStrokeType (1.4f));
}

void EmeraldLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                         bool shouldDrawButtonAsHighlighted, bool)
{
    g.setFont (juce::Font (juce::FontOptions (juce::jmin (16.0f, (float) button.getHeight() * 0.42f),
                                              juce::Font::bold)));
    g.setColour (shouldDrawButtonAsHighlighted || button.getToggleState() ? emerald::accentBright
                                                                         : emerald::text);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 2),
                      juce::Justification::centred, 2);
}

void EmeraldLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                       int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (1.0f);
    juce::Path shape;
    shape.addRoundedRectangle (bounds, 6.0f);

    g.setColour (emerald::panel);
    g.fillPath (shape);

    const bool hot = box.isMouseOver (true) || box.isPopupActive();

    if (hot)
        drawGlow (g, shape, emerald::accent, 0.85f);

    g.setColour (emerald::accent.withAlpha (hot ? 0.95f : 0.4f));
    g.strokePath (shape, juce::PathStrokeType (1.2f));

    juce::Path arrow;
    const float cx = bounds.getRight() - 14.0f;
    const float cy = bounds.getCentreY();
    arrow.startNewSubPath (cx - 5.0f, cy - 2.5f);
    arrow.lineTo (cx, cy + 3.0f);
    arrow.lineTo (cx + 5.0f, cy - 2.5f);

    g.setColour (hot ? emerald::accentBright : emerald::accent);
    g.strokePath (arrow, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void EmeraldLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getLabelFont (label));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font EmeraldLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (14.0f));
}

juce::Font EmeraldLookAndFeel::getLabelFont (juce::Label& label)
{
    return juce::Font (juce::FontOptions (juce::jlimit (11.0f, 16.0f, (float) label.getHeight() * 0.62f)));
}

juce::TextLayout EmeraldLookAndFeel::layoutTooltipText (const juce::String& text, juce::Colour colour)
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::centred);
    s.append (text, juce::Font (juce::FontOptions (13.0f, juce::Font::bold)), colour);

    juce::TextLayout layout;
    layout.createLayout (s, 300.0f);
    return layout;
}

juce::Rectangle<int> EmeraldLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                           juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltipText (tipText, emerald::accentBright);
    const int w = (int) (layout.getWidth() + 20.0f);
    const int h = (int) (layout.getHeight() + 14.0f);

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 18,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)  : screenPos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}

void EmeraldLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    juce::Path shape;
    shape.addRoundedRectangle (bounds.reduced (1.0f), 5.0f);

    g.setColour (emerald::background.withAlpha (0.96f));
    g.fillPath (shape);

    drawGlow (g, shape, emerald::accent, 0.7f);

    g.setColour (emerald::accent.withAlpha (0.9f));
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    layoutTooltipText (text, emerald::accentBright)
        .draw (g, bounds.reduced (10.0f, 7.0f));
}

} // namespace drumbreaker
