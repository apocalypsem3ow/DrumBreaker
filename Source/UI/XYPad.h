#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace drumbreaker
{

/** Interactive X/Y modulation pad driving two parameters (X = cutoff, Y = resonance). */
class XYPad : public juce::Component,
              public juce::SettableTooltipClient,
              private juce::Slider::Listener,
              private juce::Timer
{
public:
    XYPad (juce::AudioProcessorValueTreeState& apvts,
           const juce::String& xParamID,
           const juce::String& yParamID);

    ~XYPad() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setAxisNames (juce::String x, juce::String y);
    void setLiveFilterValues (float cutoff, float resonance, bool enabled);
    std::function<void()> onCentre;

private:
    void sliderValueChanged (juce::Slider*) override;
    void timerCallback() override;
    void setFromMouse (const juce::MouseEvent&);
    juce::Point<float> thumbPosition();

    juce::Slider xSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider ySlider { juce::Slider::LinearVertical,   juce::Slider::NoTextBox };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> xAttachment, yAttachment;
    juce::RangedAudioParameter* xParam = nullptr;
    juce::RangedAudioParameter* yParam = nullptr;

    juce::String xName { "Cutoff" }, yName { "Resonance" };
    bool dragging = false;
    float glow = 0.0f;
    float glowTarget = 0.0f;
    juce::Point<float> liveValues { 20000.0f, 0.707f };
    bool liveDisplay = false;
    bool doubleClickOnThumb = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYPad)
};

} // namespace drumbreaker
