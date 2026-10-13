#include "ToneStack.h"
#include "../ParameterIDs.h"

#include <cmath>

namespace drumbreaker
{

void ToneStack::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    for (auto& filter : eqFilters)
        filter.prepare (spec);
    svf.prepare (spec);
    reverb.prepare (spec);
    svf.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    juce::dsp::Reverb::Parameters reverbParameters;
    reverbParameters.wetLevel = 1.0f;
    reverbParameters.dryLevel = 0.0f;
    reverbParameters.width = 1.0f;
    reverb.setParameters (reverbParameters);

    dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
    eqBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

    for (auto* s : { &cutoffSmoothed, &resoSmoothed, &driveSmoothed, &mixSmoothed,
                     &reverbAmountSmoothed, &eqMixSmoothed, &filterEnabledSmoothed })
        s->reset (spec.sampleRate, 0.02);

    reset();
}

void ToneStack::reset()
{
    for (auto& filter : eqFilters)
        filter.reset();
    svf.reset();
    reverb.reset();
    filterPhase = 0.0;
}

ToneStack::Coeffs::Ptr ToneStack::makeEQCoefficients (size_t band, const EQBand& settings,
                                                    double rate)
{
    const float frequency = juce::jlimit (1.0f, (float) (rate * 0.49), settings.frequency);
    const float gain = juce::Decibels::decibelsToGain (juce::jlimit (-18.0f, 18.0f, settings.gainDb));

    auto type = settings.type;
    if (type == EQType::automatic)
        type = band == 0 ? EQType::lowShelf : band == 6 ? EQType::highShelf : EQType::bell;

    if (type == EQType::lowPass)
        return Coeffs::makeLowPass (rate, frequency, 0.707);
    if (type == EQType::highPass)
        return Coeffs::makeHighPass (rate, frequency, 0.707);
    if (type == EQType::lowShelf)
        return Coeffs::makeLowShelf (rate, frequency, 0.707f, gain);
    if (type == EQType::highShelf)
        return Coeffs::makeHighShelf (rate, frequency, 0.707f, gain);

    return Coeffs::makePeakFilter (rate, frequency, juce::jlimit (0.2f, 6.0f, settings.q), gain);
}

ToneStack::EQSettings ToneStack::makeEQPreset (EQPreset preset)
{
    EQSettings settings;
    for (size_t band = 0; band < settings.size(); ++band)
    {
        settings[band].frequency = params::eqDefaultFrequency (band);
        settings[band].q = band == 0 || band == 6 ? 0.707f : 0.9f;
    }
    if (preset == EQPreset::highPass || preset == EQPreset::bandPass)
    {
        settings[0].type = EQType::highPass;
        settings[0].frequency = 200.0f;
    }
    if (preset == EQPreset::lowPass || preset == EQPreset::bandPass)
    {
        settings[6].type = EQType::lowPass;
        settings[6].frequency = preset == EQPreset::bandPass ? 3000.0f : 2000.0f;
    }
    return settings;
}

void ToneStack::setEQ (const EQSettings& settings)
{
    for (size_t band = 0; band < eqFilters.size(); ++band)
        *eqFilters[band].state = *makeEQCoefficients (band, settings[band], sampleRate);
}

void ToneStack::setEQEnabled (bool enabled)
{
    eqMixSmoothed.setTargetValue (enabled ? 1.0f : 0.0f);
}

void ToneStack::setFilter (FilterType type, float cutoffHz, float resonance, float drive, float mix,
                           float modulation)
{
    switch (type)
    {
        case FilterType::bandPass: svf.setType (juce::dsp::StateVariableTPTFilterType::bandpass); break;
        case FilterType::highPass: svf.setType (juce::dsp::StateVariableTPTFilterType::highpass); break;
        case FilterType::notch:    svf.setType (juce::dsp::StateVariableTPTFilterType::bandpass); break;
        case FilterType::lowPass:
        default:                   svf.setType (juce::dsp::StateVariableTPTFilterType::lowpass);  break;
    }

    notchMode = (type == FilterType::notch);

    const float modulatedCutoff = cutoffHz * std::pow (4.0f, juce::jlimit (0.0f, 1.0f, modulation));
    cutoffSmoothed.setTargetValue (juce::jlimit (20.0f, (float) (sampleRate * 0.49), modulatedCutoff));
    resoSmoothed  .setTargetValue (juce::jlimit (0.05f, 8.0f, resonance));
    manualFilter = { type, cutoffSmoothed.getTargetValue(), resoSmoothed.getTargetValue() };
    driveSmoothed .setTargetValue (juce::jlimit (1.0f, 6.0f, drive));
    mixSmoothed   .setTargetValue (juce::jlimit (0.0f, 1.0f, mix));
}

void ToneStack::setReverb (float amount)
{
    reverbAmountSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, amount));
}

ToneStack::FilterValues ToneStack::evaluateFilterMotion (FilterMotion motion, double phase,
                                                        FilterValues manual, double rate)
{
    phase -= std::floor (phase);
    const float sweep = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * phase));
    const float maximumCutoff = juce::jmin (20000.0f, (float) (rate * 0.49));
    manual.cutoff = juce::jlimit (20.0f, maximumCutoff, manual.cutoff);
    if (motion == FilterMotion::lowPassSweep || motion == FilterMotion::highPassSweep)
    {
        manual.type = motion == FilterMotion::lowPassSweep ? FilterType::lowPass : FilterType::highPass;
        manual.cutoff = 20.0f * std::pow (maximumCutoff / 20.0f, sweep);
    }
    else if (motion == FilterMotion::resonanceModulation)
        manual.resonance = juce::jmap (sweep, 0.707f, 3.5f);
    return manual;
}

void ToneStack::setFilterEnabled (bool enabled)
{
    filterEnabled = enabled;
    filterEnabledSmoothed.setTargetValue (enabled ? 1.0f : 0.0f);
}

void ToneStack::setFilterMotion (FilterMotion motion)
{
    if (filterMotion != motion)
        filterPhase = 0.0;
    filterMotion = motion;
}

void ToneStack::setFilterPlayback (double durationSeconds, bool playing, bool restart)
{
    filterDuration = durationSeconds;
    filterPlaying = playing;
    if (restart || durationSeconds <= 0.0)
        filterPhase = 0.0;
}

void ToneStack::snapParametersToTargets()
{
    for (auto* value : { &cutoffSmoothed, &resoSmoothed, &driveSmoothed, &mixSmoothed,
                         &reverbAmountSmoothed, &eqMixSmoothed, &filterEnabledSmoothed })
        value->setCurrentAndTargetValue (value->getTargetValue());
    if (filterMotion != FilterMotion::none && filterDuration > 0.0 && filterEnabled)
    {
        const auto values = evaluateFilterMotion (filterMotion, filterPhase, manualFilter, sampleRate);
        cutoffSmoothed.setCurrentAndTargetValue (values.cutoff);
        resoSmoothed.setCurrentAndTargetValue (values.resonance);
    }
}

void ToneStack::process (juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
        return;

    eqBuffer.setSize (numChannels, numSamples, false, false, true);
    for (int channel = 0; channel < numChannels; ++channel)
        for (int sample = 0; sample < numSamples; ++sample)
            eqBuffer.setSample (channel, sample, (double) buffer.getSample (channel, sample));

    {
        juce::dsp::AudioBlock<double> block (eqBuffer);
        juce::dsp::ProcessContextReplacing<double> context (block);
        for (auto& filter : eqFilters)
            filter.process (context);
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float mix = eqMixSmoothed.getNextValue();
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float dry = buffer.getSample (channel, sample);
            buffer.setSample (channel, sample, dry + ((float) eqBuffer.getSample (channel, sample) - dry) * mix);
        }
    }

    dryBuffer.setSize (numChannels, numSamples, false, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const bool automated = filterMotion != FilterMotion::none && filterDuration > 0.0;
        if (automated && filterEnabled)
        {
            const auto values = evaluateFilterMotion (filterMotion, filterPhase, manualFilter, sampleRate);
            cutoffSmoothed.setTargetValue (values.cutoff);
            resoSmoothed.setTargetValue (values.resonance);
            svf.setType (values.type == FilterType::highPass ? juce::dsp::StateVariableTPTFilterType::highpass
                          : values.type == FilterType::bandPass || values.type == FilterType::notch
                              ? juce::dsp::StateVariableTPTFilterType::bandpass
                              : juce::dsp::StateVariableTPTFilterType::lowpass);
            notchMode = values.type == FilterType::notch;
            if (filterPlaying)
            {
                filterPhase += 1.0 / (sampleRate * filterDuration);
                filterPhase -= std::floor (filterPhase);
            }
        }
        const float cutoff = cutoffSmoothed.getNextValue();
        const float resonance = resoSmoothed.getNextValue();
        svf.setCutoffFrequency (cutoff);
        svf.setResonance (resonance);
        if (i == numSamples - 1)
        {
            liveCutoff.store (cutoff);
            liveResonance.store (resonance);
        }

        const float drive = driveSmoothed.getNextValue();
        const float mix   = mixSmoothed.getNextValue() * filterEnabledSmoothed.getNextValue();
        const float makeUp = 1.0f / std::sqrt (drive);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = dryBuffer.getSample (ch, i);
            float wet = svf.processSample (ch, std::tanh (dry * drive) * makeUp);

            if (notchMode)
                wet = dry - wet; // band-reject built from the band-pass output

            buffer.setSample (ch, i, dry + (wet - dry) * mix);
        }
    }

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    juce::dsp::AudioBlock<float> reverbBlock (buffer);
    juce::dsp::ProcessContextReplacing<float> reverbContext (reverbBlock);
    reverb.process (reverbContext);

    for (int i = 0; i < numSamples; ++i)
    {
        const float amount = reverbAmountSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, dryBuffer.getSample (ch, i) + buffer.getSample (ch, i) * amount);
    }
}

} // namespace drumbreaker
