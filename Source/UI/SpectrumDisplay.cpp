#include "SpectrumDisplay.h"
#include "EmeraldLookAndFeel.h"
#include "../ParameterIDs.h"

#include <cmath>

namespace drumbreaker
{

SpectrumDisplay::SpectrumDisplay (SpectrumTap& tapToUse, juce::AudioProcessorValueTreeState& parameters)
    : tap (tapToUse), state (parameters)
{
    setOpaque (false);
    for (auto* slider : { &gainSlider, &frequencySlider, &widthSlider })
    {
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        addAndMakeVisible (*slider);
    }
    for (auto* label : { &gainLabel, &frequencyLabel, &widthLabel })
    {
        label->setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        addAndMakeVisible (*label);
    }
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setTooltip ("Selected band gain: -18 to +18 dB");
    frequencySlider.setTooltip ("Selected band frequency: 20 to 20000 Hz");
    widthSlider.setTooltip ("Selected band Q: higher values give a narrower band");
    updateResponse();
    selectBand (3);
    startTimerHz (30);
}

SpectrumDisplay::~SpectrumDisplay()
{
    stopTimer();
    finishGesture();
}

juce::Rectangle<float> SpectrumDisplay::graphArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (94.0f).reduced (32.0f, 15.0f);
}

juce::Rectangle<float> SpectrumDisplay::heatArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (48.0f).removeFromBottom (28.0f).reduced (4.0f, 0.0f);
}

float SpectrumDisplay::frequencyToX (float frequency) const
{
    const auto area = graphArea();
    return area.getX() + juce::jlimit (0.0f, 1.0f, params::eqPositionAtFrequency (frequency)) * area.getWidth();
}

float SpectrumDisplay::gainToY (float gain) const
{
    const auto area = graphArea();
    return area.getCentreY() - juce::jlimit (-18.0f, 18.0f, gain) * area.getHeight() / 36.0f;
}

float SpectrumDisplay::responseGainAtFrequency (double frequency) const
{
    if (! eqEnabled)
        return 0.0f;
    frequency = juce::jmin (frequency, responseSampleRate * 0.49);
    double magnitude = 1.0;
    for (const auto& coefficient : coefficients)
        if (coefficient != nullptr)
            magnitude *= coefficient->getMagnitudeForFrequency (frequency, responseSampleRate);
    return (float) juce::Decibels::gainToDecibels (magnitude, -72.0);
}

juce::Point<float> SpectrumDisplay::bandPosition (size_t band) const
{
    float gain = eqSettings[band].gainDb;
    if (isCutBand (band) && coefficients[band] != nullptr)
        gain = (float) juce::Decibels::gainToDecibels (coefficients[band]->getMagnitudeForFrequency (
            juce::jmin ((double) eqSettings[band].frequency, responseSampleRate * 0.49), responseSampleRate));
    return { frequencyToX (eqSettings[band].frequency), gainToY (eqEnabled ? gain : 0.0f) };
}

bool SpectrumDisplay::isCutBand (size_t band) const
{
    return eqSettings[band].type == ToneStack::EQType::lowPass
        || eqSettings[band].type == ToneStack::EQType::highPass;
}

void SpectrumDisplay::updateControlState()
{
    const bool cut = isCutBand ((size_t) selectedBand);
    gainSlider.setEnabled (eqEnabled && ! cut);
    frequencySlider.setEnabled (eqEnabled);
    widthSlider.setEnabled (eqEnabled && ! cut);
    gainLabel.setAlpha (eqEnabled ? 1.0f : 0.3f);
    frequencyLabel.setAlpha (eqEnabled ? 1.0f : 0.3f);
    widthLabel.setAlpha (eqEnabled ? 1.0f : 0.3f);
}

void SpectrumDisplay::updateResponse()
{
    const double rate = tap.getSampleRate();
    const bool nextEnabled = state.getRawParameterValue (params::eqEnabled)->load() >= 0.5f;
    bool changed = rate != responseSampleRate || nextEnabled != eqEnabled;
    bool selectedTypeChanged = false;
    eqEnabled = nextEnabled;
    if (! eqEnabled)
        finishGesture();
    for (size_t band = 0; band < eqSettings.size(); ++band)
    {
        const ToneStack::EQBand next {
            state.getRawParameterValue (params::eqGainIDs[band])->load(),
            state.getRawParameterValue (params::eqFrequencyIDs[band])->load(),
            params::eqQIDs[band] != nullptr ? state.getRawParameterValue (params::eqQIDs[band])->load() : 0.707f,
            (ToneStack::EQType) (int) state.getRawParameterValue (params::eqTypeIDs[band])->load() };
        if ((int) band == selectedBand && next.type != eqSettings[band].type)
            selectedTypeChanged = true;
        changed = changed || next.gainDb != eqSettings[band].gainDb
                          || next.frequency != eqSettings[band].frequency || next.q != eqSettings[band].q
                          || next.type != eqSettings[band].type;
        eqSettings[band] = next;
    }
    if (changed)
    {
        responseSampleRate = rate;
        for (size_t band = 0; band < coefficients.size(); ++band)
            coefficients[band] = ToneStack::makeEQCoefficients (band, eqSettings[band], rate);
        if (selectedTypeChanged)
            selectBand (selectedBand);
        updateControlState();
        repaint();
    }
}

void SpectrumDisplay::selectBand (int band)
{
    selectedBand = band;
    gainAttachment.reset();
    frequencyAttachment.reset();
    widthAttachment.reset();
    const auto index = (size_t) band;
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::eqGainIDs[index], gainSlider);
    frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::eqFrequencyIDs[index], frequencySlider);
    const bool hasWidth = params::eqQIDs[index] != nullptr && ! isCutBand (index);
    widthSlider.setVisible (hasWidth);
    widthLabel.setVisible (hasWidth);
    if (hasWidth)
        widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, params::eqQIDs[index], widthSlider);
    const auto type = eqSettings[index].type;
    gainLabel.setText ("BAND " + juce::String (band + 1)
                       + (type == ToneStack::EQType::lowPass ? " / LP" : type == ToneStack::EQType::highPass ? " / HP" : " / GAIN"),
                       juce::dontSendNotification);
    frequencyLabel.setText (isCutBand (index) ? "CUTOFF" : "FREQUENCY", juce::dontSendNotification);
    widthLabel.setText ("WIDTH / Q", juce::dontSendNotification);
    updateTooltip (band);
    updateControlState();
    repaint();
}

void SpectrumDisplay::updateTooltip (int band)
{
    if (band < 0)
    {
        setTooltip ("Seven-band EQ: drag frequency/gain, scroll for width, double-click to reset a band");
        return;
    }
    const auto& settings = eqSettings[(size_t) band];
    if (isCutBand ((size_t) band))
    {
        setTooltip ("Band " + juce::String (band + 1)
                    + (settings.type == ToneStack::EQType::lowPass ? " / Low Pass: " : " / High Pass: ")
                    + juce::String (settings.frequency, 1) + " Hz cutoff");
        return;
    }
    setTooltip ("Band " + juce::String (band + 1) + ": " + juce::String (settings.frequency, 1)
                + " Hz / " + juce::String (settings.gainDb, 1) + " dB");
}

int SpectrumDisplay::hitTestBand (juce::Point<float> point, bool& endpoint) const
{
    endpoint = false;
    if (! eqEnabled)
        return -1;
    int closest = -1;
    float distance = 13.0f;
    for (size_t band = 0; band < eqSettings.size(); ++band)
    {
        const float next = point.getDistanceFrom (bandPosition (band));
        if (next < distance)
        {
            distance = next;
            closest = (int) band;
        }
    }
    if (closest >= 0)
        return closest;
    for (int band : { 0, 6 })
    {
        if (isCutBand ((size_t) band))
            continue;
        const auto area = graphArea();
        const juce::Point<float> handle (band == 0 ? area.getX() : area.getRight(),
            gainToY (responseGainAtFrequency (band == 0 ? params::eqMinFrequency : params::eqMaxFrequency)));
        if (point.getDistanceFrom (handle) < 10.0f)
        {
            endpoint = true;
            return band;
        }
    }
    return -1;
}

void SpectrumDisplay::mouseMove (const juce::MouseEvent& event)
{
    bool endpoint = false;
    hoveredBand = hitTestBand (event.position, endpoint);
    setMouseCursor (hoveredBand >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    updateTooltip (hoveredBand);
    repaint();
}

void SpectrumDisplay::mouseExit (const juce::MouseEvent&)
{
    hoveredBand = -1;
    repaint();
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown())
        return;
    finishGesture();
    updateResponse();
    draggingBand = hitTestBand (event.position, draggingEndpoint);
    if (draggingBand < 0)
        return;
    selectBand (draggingBand);
    const auto index = (size_t) draggingBand;
    draggingCut = isCutBand (index);
    dragOffset = bandPosition (index) - event.position;
    if (! draggingCut)
        state.getParameter (params::eqGainIDs[index])->beginChangeGesture();
    if (! draggingEndpoint)
        state.getParameter (params::eqFrequencyIDs[index])->beginChangeGesture();
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingBand < 0 || ! eqEnabled)
        return;
    const auto index = (size_t) draggingBand;
    const auto area = graphArea();
    const auto position = event.position + dragOffset;
    const float gain = juce::jlimit (-18.0f, 18.0f,
        (area.getCentreY() - position.y) * 36.0f / juce::jmax (1.0f, area.getHeight()));
    if (! draggingCut)
    {
        auto* gainParameter = state.getParameter (params::eqGainIDs[index]);
        gainParameter->setValueNotifyingHost (gainParameter->convertTo0to1 (gain));
    }
    if (! draggingEndpoint)
    {
        const float proportion = juce::jlimit (0.0f, 1.0f,
            (position.x - area.getX()) / juce::jmax (1.0f, area.getWidth()));
        auto* frequencyParameter = state.getParameter (params::eqFrequencyIDs[index]);
        frequencyParameter->setValueNotifyingHost (
            frequencyParameter->convertTo0to1 (params::eqFrequencyAtPosition (proportion)));
    }
    updateResponse();
    updateTooltip (draggingBand);
}

void SpectrumDisplay::finishGesture()
{
    if (draggingBand < 0)
        return;
    const auto index = (size_t) draggingBand;
    if (! draggingCut)
        state.getParameter (params::eqGainIDs[index])->endChangeGesture();
    if (! draggingEndpoint)
        state.getParameter (params::eqFrequencyIDs[index])->endChangeGesture();
    draggingBand = -1;
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    finishGesture();
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& event)
{
    finishGesture();
    bool endpoint = false;
    const int band = hitTestBand (event.position, endpoint);
    if (band < 0)
        return;
    const auto index = (size_t) band;
    for (const char* id : { params::eqGainIDs[index], endpoint ? nullptr : params::eqFrequencyIDs[index],
                            endpoint ? nullptr : params::eqQIDs[index], endpoint ? nullptr : params::eqTypeIDs[index] })
        if (id != nullptr)
        {
            auto* parameter = state.getParameter (id);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
            parameter->endChangeGesture();
        }
    updateResponse();
}

void SpectrumDisplay::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    bool endpoint = false;
    const int band = hitTestBand (event.position, endpoint);
    if (band < 0 || endpoint || isCutBand ((size_t) band) || params::eqQIDs[(size_t) band] == nullptr || draggingBand >= 0)
        return;
    selectBand (band);
    auto* parameter = state.getParameter (params::eqQIDs[(size_t) band]);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, parameter->getValue() + wheel.deltaY * 0.15f));
    parameter->endChangeGesture();
    updateResponse();
}

void SpectrumDisplay::resized()
{
    const int w = juce::jmax (2, (int) heatArea().getWidth());
    const int h = juce::jmax (1, (int) heatArea().getHeight());

    heatMap = juce::Image (juce::Image::RGB, w, h, true);
    heatMap.clear (heatMap.getBounds(), juce::Colours::black);
    heatColumn = 0;

    auto controls = getLocalBounds().removeFromBottom (44).reduced (6, 0);
    const int width = controls.getWidth() / 3;
    juce::Slider* sliders[] { &gainSlider, &frequencySlider, &widthSlider };
    juce::Label* labels[] { &gainLabel, &frequencyLabel, &widthLabel };
    for (size_t index = 0; index < 3; ++index)
    {
        auto slot = controls.removeFromLeft (width).reduced (3, 0);
        labels[index]->setBounds (slot.removeFromTop (14));
        sliders[index]->setBounds (slot);
    }
}

void SpectrumDisplay::timerCallback()
{
    updateResponse();
    if (! tap.getNextFrame (fftData.data()))
        return;

    std::fill (fftData.begin() + SpectrumTap::fftSize, fftData.end(), 0.0f);

    window.multiplyWithWindowingTable (fftData.data(), (size_t) SpectrumTap::fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const int numBins = SpectrumTap::fftSize / 2;

    for (int b = 0; b < numBands; ++b)
    {
        // Logarithmic band spacing so the low end is not squashed into one bar.
        const float lowProp  = (float) b / (float) numBands;
        const float highProp = (float) (b + 1) / (float) numBands;

        const double rate = tap.getSampleRate();
        const int from = juce::jlimit (1, numBins - 1,
            (int) (params::eqFrequencyAtPosition (lowProp) * SpectrumTap::fftSize / rate));
        const int to = juce::jlimit (from + 1, numBins,
            (int) (params::eqFrequencyAtPosition (highProp) * SpectrumTap::fftSize / rate) + 1);

        float sum = 0.0f;

        for (int i = from; i < to; ++i)
            sum = juce::jmax (sum, fftData[(size_t) i]);

        const float db    = juce::Decibels::gainToDecibels (sum + 1.0e-9f);
        const float level = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -80.0f, 0.0f, 0.0f, 1.0f));

        // Fast attack, slow release makes the histogram readable.
        bands[(size_t) b] = level > bands[(size_t) b] ? level
                                                      : bands[(size_t) b] * 0.82f + level * 0.18f;
    }

    if (heatMap.isValid())
    {
        const int h = heatMap.getHeight();
        heatColumn = (heatColumn + 1) % heatMap.getWidth();

        juce::Image::BitmapData pixels (heatMap, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < h; ++y)
        {
            const float prop = 1.0f - (float) y / (float) juce::jmax (1, h - 1);
            const int   band = juce::jlimit (0, numBands - 1, (int) (prop * (numBands - 1)));
            pixels.setPixelColour (heatColumn, y, heatColour (bands[(size_t) band]));
        }
    }

    repaint();
}

juce::Colour SpectrumDisplay::heatColour (float level)
{
    level = juce::jlimit (0.0f, 1.0f, level);

    // black -> deep green -> emerald -> mint -> white hot
    if (level < 0.35f)
        return juce::Colour::fromFloatRGBA (0.0f, level * 1.2f, level * 0.7f, 1.0f);

    if (level < 0.75f)
        return emerald::accent.withMultipliedBrightness (0.6f + level * 0.8f);

    return emerald::accent.interpolatedWith (juce::Colours::white, (level - 0.75f) / 0.25f);
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (emerald::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto heat = heatArea();
    const auto graph = graphArea();
    if (graph.getWidth() <= 0.0f || graph.getHeight() <= 0.0f)
        return;

    g.setFont (juce::Font (juce::FontOptions (9.5f)));
    for (int gain : { -18, -9, 0, 9, 18 })
    {
        const float position = gainToY ((float) gain);
        g.setColour (emerald::accent.withAlpha (gain == 0 ? 0.25f : 0.08f));
        g.drawLine (graph.getX(), position, graph.getRight(), position, gain == 0 ? 1.2f : 1.0f);
        g.setColour (emerald::textDim);
        g.drawText ((gain > 0 ? "+" : "") + juce::String (gain),
                    juce::Rectangle<float> (0.0f, position - 6.0f, 27.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    for (float frequency : { 20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f })
    {
        const float position = frequencyToX (frequency);
        g.setColour (emerald::accent.withAlpha (0.08f));
        g.drawLine (position, graph.getY(), position, graph.getBottom());
        g.setColour (emerald::textDim);
        const auto text = frequency >= 1000.0f ? juce::String ((int) frequency / 1000) + "k"
                                              : juce::String ((int) frequency);
        g.drawText (text, juce::Rectangle<float> (position - 18.0f, graph.getBottom() + 3.0f, 36.0f, 13.0f),
                    juce::Justification::centred);
    }

    const float barWidth = graph.getWidth() / (float) numBands;

    juce::Path outline;
    outline.startNewSubPath (graph.getX(), graph.getBottom());

    for (int b = 0; b < numBands; ++b)
    {
        const float level = bands[(size_t) b];
        const float h = graph.getHeight() * level;
        const juce::Rectangle<float> bar (graph.getX() + barWidth * (float) b,
                          graph.getBottom() - h,
                                          barWidth * 0.82f,
                                          h);

        g.setGradientFill (juce::ColourGradient (emerald::accent.withAlpha (0.24f), bar.getX(), bar.getY(),
                             emerald::accentDark.withAlpha (0.12f), bar.getX(), graph.getBottom(),
                                                 false));
        g.fillRoundedRectangle (bar, 1.5f);

        outline.lineTo (bar.getCentreX(), bar.getY());
    }

    outline.lineTo (graph.getRight(), graph.getBottom());

    g.setColour (emerald::accentBright.withAlpha (0.20f));
    g.strokePath (outline, juce::PathStrokeType (1.0f));

    juce::Path response;
    const int columns = juce::jmax (2, (int) graph.getWidth());
    for (int column = 0; column <= columns; ++column)
    {
        const float proportion = (float) column / (float) columns;
        const float gain = responseGainAtFrequency (params::eqFrequencyAtPosition (proportion));
        const float position = graph.getX() + proportion * graph.getWidth();
        if (column == 0)
            response.startNewSubPath (position, gainToY (gain));
        else
            response.lineTo (position, gainToY (gain));
    }
    if (eqEnabled)
        EmeraldLookAndFeel::drawGlow (g, response, emerald::accent, 0.45f);
    g.setColour (eqEnabled ? emerald::accentBright : emerald::textDim.withAlpha (0.4f));
    g.strokePath (response, juce::PathStrokeType (1.8f));

    for (int band : { 0, 6 })
    {
        const juce::Point<float> point (band == 0 ? graph.getX() : graph.getRight(),
            gainToY (responseGainAtFrequency (band == 0 ? params::eqMinFrequency : params::eqMaxFrequency)));
        g.setColour (eqEnabled ? emerald::accentBright : emerald::textDim.withAlpha (0.4f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (point));
    }

    const std::array<juce::uint32, 7> colours {
        0xff6fdaaa, 0xff77cce5, 0xff9bd772, 0xff1fd692, 0xffedcf81, 0xfff3a3a3, 0xffa3b5ed };
    for (size_t band = 0; band < eqSettings.size(); ++band)
    {
        const auto point = bandPosition (band);
        const auto colour = juce::Colour (colours[band]).withMultipliedAlpha (eqEnabled ? 1.0f : 0.3f);
        const bool active = eqEnabled && ((int) band == selectedBand || (int) band == hoveredBand);
        juce::Path token;
        token.addEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (point));
        if (active)
            EmeraldLookAndFeel::drawGlow (g, token, colour, 0.6f);
        g.setColour (emerald::background);
        g.fillPath (token);
        g.setColour (colour);
        g.strokePath (token, juce::PathStrokeType (active ? 2.0f : 1.2f));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (juce::String ((int) band + 1), juce::Rectangle<float> (18.0f, 18.0f).withCentre (point),
                    juce::Justification::centred);
    }

    if (heatMap.isValid())
    {
        // Draw the scrolling spectrogram so the newest column sits on the right.
        const int w     = heatMap.getWidth();
        const int split = juce::jlimit (1, w - 1, heatColumn + 1);
        const auto dest = heat.toNearestInt();

        const int olderW = w - split;
        const int destOlderW = juce::roundToInt ((float) dest.getWidth() * (float) olderW / (float) w);

        g.drawImage (heatMap, dest.getX(), dest.getY(), destOlderW, dest.getHeight(),
                     split, 0, olderW, heatMap.getHeight());

        g.drawImage (heatMap, dest.getX() + destOlderW, dest.getY(),
                     dest.getWidth() - destOlderW, dest.getHeight(),
                     0, 0, split, heatMap.getHeight());
    }

    g.setColour (emerald::accent.withAlpha (0.35f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}

} // namespace drumbreaker
