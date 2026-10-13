#pragma once

#include "Audio/ToneStack.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>

namespace drumbreaker
{

/** A genre-flavoured snapshot of the EQ + filter section. */
struct FilterPreset
{
    const char* name;
    const char* hint;

    ToneStack::FilterType type;
    float cutoffHz;
    float resonance;
    float drive;
    float mix;

    float lowGainDb,  lowFreq;
    float midGainDb,  midFreq, midQ;
    float highGainDb, highFreq;
};

/** Index 0 is "Custom": selecting it never overwrites the user's knobs. */
const std::vector<FilterPreset>& getFilterPresets();

juce::StringArray getFilterPresetNames();

/** Pushes a preset's values into the value tree (no-op for "Custom"). */
void applyFilterPreset (juce::AudioProcessorValueTreeState& apvts, int presetIndex);
void applyEQPreset (juce::AudioProcessorValueTreeState& apvts, ToneStack::EQPreset preset);

} // namespace drumbreaker
