#include "XYPad.h"
#include "EmeraldLookAndFeel.h"

namespace drumbreaker
{

XYPad::XYPad (juce::AudioProcessorValueTreeState& apvts,
              const juce::String& xParamID,
              const juce::String& yParamID)
{
    for (auto* s : { &xSlider, &ySlider })
    {
        addChildComponent (*s);        // kept invisible: the pad is the visible control
        s->addListener (this);
    }

    xAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, xParamID, xSlider);
    yAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, yParamID, ySlider);

    xParam = apvts.getParameter (xParamID);
    yParam = apvts.getParameter (yParamID);

    startTimerHz (30);
}

XYPad::~XYPad()
{
    xSlider.removeListener (this);
    ySlider.removeListener (this);
}

void XYPad::setAxisNames (juce::String x, juce::String y)
{
    xName = std::move (x);
    yName = std::move (y);
    repaint();
}

void XYPad::setLiveFilterValues (float cutoff, float resonance, bool enabled)
{
    if (! enabled && dragging)
    {
        dragging = false;
        for (auto* parameter : { xParam, yParam })
            if (parameter != nullptr)
                parameter->endChangeGesture();
    }
    const juce::Point<float> next { cutoff, resonance };
    const bool changed = ! liveDisplay || next != liveValues || enabled != isEnabled();
    liveValues = next;
    liveDisplay = true;
    setEnabled (enabled);
    if (changed)
        repaint();
}

void XYPad::sliderValueChanged (juce::Slider*)
{
    repaint();
}

void XYPad::timerCallback()
{
    const float target = dragging ? 1.0f : glowTarget;

    if (std::abs (glow - target) > 0.005f)
    {
        glow += (target - glow) * 0.25f;
        repaint();
    }
}

juce::Point<float> XYPad::thumbPosition()
{
    auto area = getLocalBounds().toFloat().reduced (8.0f);

    const float x = liveDisplay && ! dragging && xParam != nullptr
        ? juce::jlimit (0.0f, 1.0f, xParam->convertTo0to1 (liveValues.x))
        : (float) xSlider.valueToProportionOfLength (xSlider.getValue());
    const float y = liveDisplay && ! dragging && yParam != nullptr
        ? juce::jlimit (0.0f, 1.0f, yParam->convertTo0to1 (liveValues.y))
        : (float) ySlider.valueToProportionOfLength (ySlider.getValue());

    return { area.getX() + area.getWidth() * x,
             area.getBottom() - area.getHeight() * y };
}

void XYPad::setFromMouse (const juce::MouseEvent& e)
{
    auto area = getLocalBounds().toFloat().reduced (8.0f);

    const float px = juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / juce::jmax (1.0f, area.getWidth()));
    const float py = juce::jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / juce::jmax (1.0f, area.getHeight()));

    xSlider.setValue (xSlider.proportionOfLengthToValue (px), juce::sendNotificationSync);
    ySlider.setValue (ySlider.proportionOfLengthToValue (py), juce::sendNotificationSync);
}

void XYPad::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! e.mods.isLeftButtonDown())
        return;
    if (e.getNumberOfClicks() > 1)
        return;
    doubleClickOnThumb = e.position.getDistanceFrom (thumbPosition()) <= 12.0f;
    dragging = true;

    for (auto* p : { xParam, yParam })
        if (p != nullptr)
            p->beginChangeGesture();

    setFromMouse (e);
}

void XYPad::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging && isEnabled())
        setFromMouse (e);
}

void XYPad::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;

    for (auto* p : { xParam, yParam })
        if (p != nullptr)
            p->endChangeGesture();

    repaint();
}

void XYPad::mouseEnter (const juce::MouseEvent&)
{
    glowTarget = 0.85f;
}

void XYPad::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (! isEnabled() || ! event.mods.isLeftButtonDown() || ! doubleClickOnThumb)
        return;
    if (dragging)
        mouseUp (event);
    if (onCentre != nullptr)
        onCentre();

    for (auto* parameter : { xParam, yParam })
        if (parameter != nullptr)
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (0.5f);
            parameter->endChangeGesture();
        }
    if (xParam != nullptr && yParam != nullptr)
        liveValues = { xParam->convertFrom0to1 (0.5f), yParam->convertFrom0to1 (0.5f) };
    glow = 1.0f;
    doubleClickOnThumb = false;
    repaint();
}

void XYPad::mouseExit (const juce::MouseEvent&)
{
    glowTarget = 0.0f;
}

void XYPad::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::Path frame;
    frame.addRoundedRectangle (bounds.reduced (1.0f), 6.0f);

    g.setColour (emerald::panel);
    g.fillPath (frame);

    auto area = bounds.reduced (8.0f);

    g.setColour (emerald::accent.withAlpha (0.10f));

    for (int i = 1; i < 4; ++i)
    {
        g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / 4.0f), area.getX(), area.getRight());
        g.drawVerticalLine   ((int) (area.getX() + area.getWidth()  * (float) i / 4.0f), area.getY(), area.getBottom());
    }

    const auto thumb = thumbPosition();
    const float intensity = isEnabled() ? 0.35f + glow * 0.65f : 0.05f;

    // crosshair
    g.setColour (emerald::accent.withAlpha (0.25f + glow * 0.35f));
    g.drawHorizontalLine ((int) thumb.y, area.getX(), area.getRight());
    g.drawVerticalLine   ((int) thumb.x, area.getY(), area.getBottom());

    // glow halo
    const float haloRadius = 26.0f + glow * 12.0f;
    g.setGradientFill (juce::ColourGradient (emerald::accent.withAlpha (0.45f * intensity), thumb.x, thumb.y,
                                             emerald::accent.withAlpha (0.0f), thumb.x + haloRadius, thumb.y, true));
    g.fillEllipse (juce::Rectangle<float> (haloRadius * 2.0f, haloRadius * 2.0f).withCentre (thumb));

    juce::Path dot;
    dot.addEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (thumb));
    EmeraldLookAndFeel::drawGlow (g, dot, emerald::accentBright, intensity);

    g.setColour (isEnabled() ? (dragging ? juce::Colours::white : emerald::accentBright)
                             : emerald::textDim.withAlpha (0.35f));
    g.fillPath (dot);

    // axis labels
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.setColour (emerald::textDim);
    g.drawText (xName, bounds.reduced (8.0f, 2.0f), juce::Justification::bottomRight, false);

    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                     bounds.getX() + 10.0f, bounds.getCentreY()));
    g.drawText (yName, juce::Rectangle<float> (bounds.getX() - 30.0f, bounds.getCentreY() - 8.0f, 80.0f, 16.0f),
                juce::Justification::centred, false);
    g.restoreState();

    g.setColour (emerald::accent.withAlpha (0.35f + glow * 0.5f));
    g.strokePath (frame, juce::PathStrokeType (1.2f));
}

} // namespace drumbreaker
