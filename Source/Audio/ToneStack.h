#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

namespace drumbreaker
{

/** Seven band EQ followed by a resonant filter and reverb. */
class ToneStack
{
public:
    enum class FilterType { lowPass = 0, bandPass, highPass, notch };
    enum class EQType { automatic = 0, bell, lowShelf, highShelf, lowPass, highPass };
    enum class EQPreset { flat = 0, lowPass, bandPass, highPass };
    enum class FilterMotion { none = 0, lowPassSweep, highPassSweep, resonanceModulation };

    struct FilterValues
    {
        FilterType type = FilterType::lowPass;
        float cutoff = 20000.0f;
        float resonance = 0.707f;
    };

    static FilterValues evaluateFilterMotion (FilterMotion, double phase, FilterValues manual, double sampleRate);

    struct EQBand
    {
        float gainDb = 0.0f;
        float frequency = 1000.0f;
        float q = 0.9f;
        EQType type = EQType::automatic;
    };

    using EQSettings = std::array<EQBand, 7>;
    using Coeffs = juce::dsp::IIR::Coefficients<double>;
    static Coeffs::Ptr makeEQCoefficients (size_t band, const EQBand&, double sampleRate);
    static EQSettings makeEQPreset (EQPreset preset);

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    void setEQ (const EQSettings&);
    void setEQEnabled (bool enabled);

    void setFilter (FilterType type, float cutoffHz, float resonance, float drive, float mix,
                    float modulation);
    void setReverb (float amount);
    void setFilterEnabled (bool enabled);
    void setFilterMotion (FilterMotion motion);
    void setFilterPlayback (double durationSeconds, bool playing, bool restart);
    void snapParametersToTargets();
    float getLiveFilterCutoff() const noexcept { return liveCutoff.load(); }
    float getLiveFilterResonance() const noexcept { return liveResonance.load(); }
    double getFilterMotionPhase() const noexcept { return filterPhase; }

    void process (juce::AudioBuffer<float>& buffer);

private:
    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<double>, Coeffs>;

    double sampleRate = 44100.0;

    std::array<Duplicator, 7> eqFilters;
    juce::dsp::StateVariableTPTFilter<float> svf;
    juce::dsp::Reverb reverb;

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<double> eqBuffer;

    juce::SmoothedValue<float> cutoffSmoothed { 20000.0f };
    juce::SmoothedValue<float> resoSmoothed   { 0.707f };
    juce::SmoothedValue<float> driveSmoothed  { 1.0f };
    juce::SmoothedValue<float> mixSmoothed    { 1.0f };
    juce::SmoothedValue<float> reverbAmountSmoothed { 0.0f };
    juce::SmoothedValue<float> eqMixSmoothed { 1.0f };
    juce::SmoothedValue<float> filterEnabledSmoothed { 1.0f };

    FilterValues manualFilter;
    FilterMotion filterMotion = FilterMotion::none;
    double filterPhase = 0.0;
    double filterDuration = 0.0;
    bool filterPlaying = false;
    bool filterEnabled = true;
    std::atomic<float> liveCutoff { 20000.0f };
    std::atomic<float> liveResonance { 0.707f };

    bool notchMode = false;
};

} // namespace drumbreaker
