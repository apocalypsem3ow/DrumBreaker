#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Audio/ToneStack.h"
#include <array>
#include <atomic>

namespace drumbreaker
{

/** Lock-free hand-off of audio from the processor to the spectrum UI. */
class SpectrumTap
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;

    void setSampleRate (double rate) noexcept { sampleRate.store (rate); }
    double getSampleRate() const noexcept { return sampleRate.load(); }

    void pushBuffer (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();

        if (numChannels <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                mono += buffer.getSample (ch, i);

            pushSample (mono / (float) numChannels);
        }
    }

    void pushSample (float sample) noexcept
    {
        if (fifoIndex == fftSize)
        {
            if (! frameReady.load())
            {
                std::copy (fifo.begin(), fifo.end(), frame.begin());
                frameReady.store (true);
            }

            fifoIndex = 0;
        }

        fifo[(size_t) fifoIndex++] = sample;
    }

    /** Copies the latest frame into dest (must hold fftSize floats). */
    bool getNextFrame (float* dest) noexcept
    {
        if (! frameReady.load())
            return false;

        std::copy (frame.begin(), frame.end(), dest);
        frameReady.store (false);
        return true;
    }

private:
    std::array<float, fftSize> fifo  {};
    std::array<float, fftSize> frame {};
    int fifoIndex = 0;
    std::atomic<bool> frameReady { false };
    std::atomic<double> sampleRate { 44100.0 };
};

/** Frequency histogram with a scrolling heat-map underneath. */
class SpectrumDisplay : public juce::Component,
                        public juce::SettableTooltipClient,
                        private juce::Timer
{
public:
    SpectrumDisplay (SpectrumTap& tapToUse, juce::AudioProcessorValueTreeState&);
    ~SpectrumDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    static juce::Colour heatColour (float level);
    juce::Rectangle<float> graphArea() const;
    juce::Rectangle<float> heatArea() const;
    float frequencyToX (float frequency) const;
    float gainToY (float gain) const;
    float responseGainAtFrequency (double frequency) const;
    juce::Point<float> bandPosition (size_t band) const;
    int hitTestBand (juce::Point<float>, bool& endpoint) const;
    void selectBand (int band);
    void finishGesture();
    void updateResponse();
    void updateTooltip (int band);
    bool isCutBand (size_t band) const;
    void updateControlState();

    static constexpr int numBands = 48;

    SpectrumTap& tap;
    juce::AudioProcessorValueTreeState& state;
    ToneStack::EQSettings eqSettings;
    std::array<ToneStack::Coeffs::Ptr, 7> coefficients;
    double responseSampleRate = 0.0;
    int selectedBand = 3;
    int hoveredBand = -1;
    int draggingBand = -1;
    bool draggingEndpoint = false;
    bool draggingCut = false;
    bool eqEnabled = true;
    juce::Point<float> dragOffset;

    juce::Slider gainSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider frequencySlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider widthSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label gainLabel, frequencyLabel, widthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequencyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;

    juce::dsp::FFT fft { SpectrumTap::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) SpectrumTap::fftSize,
                                                 juce::dsp::WindowingFunction<float>::hann };

    std::array<float, SpectrumTap::fftSize * 2> fftData {};
    std::array<float, numBands> bands {};

    juce::Image heatMap;
    int heatColumn = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};

} // namespace drumbreaker
