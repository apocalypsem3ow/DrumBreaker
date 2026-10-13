#include "FilterPresets.h"
#include "ParameterIDs.h"

namespace drumbreaker
{

const std::vector<FilterPreset>& getFilterPresets()
{
    using FT = ToneStack::FilterType;

    static const std::vector<FilterPreset> presets
    {
        { "Custom", "Your own filter and EQ settings",
          FT::lowPass, 20000.0f, 0.707f, 1.0f, 1.0f,
          0.0f, 120.0f, 0.0f, 1000.0f, 0.9f, 0.0f, 8000.0f },

        { "Hip Hop", "Warm low-pass, fat low end, tape-style top roll off",
          FT::lowPass, 7500.0f, 0.80f, 1.6f, 1.0f,
          4.5f, 90.0f, -1.5f, 550.0f, 0.8f, -2.0f, 9000.0f },

        { "Trap", "Sub heavy with a scooped mid and crisp hats",
          FT::lowPass, 12000.0f, 1.10f, 2.2f, 1.0f,
          6.5f, 60.0f, -4.0f, 700.0f, 1.1f, 3.5f, 10000.0f },

        { "Breakcore", "Aggressive resonant band-pass and heavy drive",
          FT::bandPass, 1800.0f, 3.40f, 4.5f, 0.85f,
          2.0f, 110.0f, 5.0f, 2200.0f, 2.0f, 4.0f, 7500.0f },

        { "Jungle", "High-pass amen snap with a resonant bite",
          FT::highPass, 180.0f, 1.60f, 2.4f, 0.9f,
          3.0f, 70.0f, 2.5f, 3000.0f, 1.4f, 2.0f, 9500.0f },

        { "Techno", "Tight notch groove, driven and mid-forward",
          FT::notch, 900.0f, 2.60f, 3.0f, 0.75f,
          1.5f, 85.0f, -2.5f, 1200.0f, 1.8f, 1.5f, 11000.0f },

        { "House", "Classic filtered-loop low-pass with lifted body",
          FT::lowPass, 4200.0f, 1.40f, 1.8f, 1.0f,
          2.5f, 100.0f, 1.5f, 900.0f, 0.9f, -1.0f, 8500.0f },

        { "Trance", "Sweeping high-resonance low-pass, airy top",
          FT::lowPass, 2600.0f, 4.20f, 1.4f, 1.0f,
          0.5f, 120.0f, -1.0f, 1500.0f, 1.2f, 5.0f, 12000.0f }
    };

    return presets;
}

juce::StringArray getFilterPresetNames()
{
    juce::StringArray names;

    for (const auto& p : getFilterPresets())
        names.add (p.name);

    return names;
}

static void setParam (juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void applyFilterPreset (juce::AudioProcessorValueTreeState& apvts, int presetIndex)
{
    const auto& presets = getFilterPresets();

    if (presetIndex <= 0 || presetIndex >= (int) presets.size())
        return;

    const auto& p = presets[(size_t) presetIndex];

    for (const char* id : params::eqTypeIDs)
      setParam (apvts, id, 0.0f);

    setParam (apvts, params::filterType,   (float) (int) p.type);
    setParam (apvts, params::filterCutoff, p.cutoffHz);
    setParam (apvts, params::filterReso,   p.resonance);
    setParam (apvts, params::filterDrive,  p.drive);
    setParam (apvts, params::filterMix,    p.mix);

    setParam (apvts, params::eqLowGain,  p.lowGainDb);
    setParam (apvts, params::eqLowFreq,  p.lowFreq);
    setParam (apvts, params::eqMidGain,  p.midGainDb);
    setParam (apvts, params::eqMidFreq,  p.midFreq);
    setParam (apvts, params::eqMidQ,     p.midQ);
    setParam (apvts, params::eqHighGain, p.highGainDb);
    setParam (apvts, params::eqHighFreq, p.highFreq);

    for (size_t band : { (size_t) 1, (size_t) 2, (size_t) 4, (size_t) 5 })
    {
      setParam (apvts, params::eqGainIDs[band], 0.0f);
      setParam (apvts, params::eqFrequencyIDs[band], params::eqDefaultFrequency (band));
      setParam (apvts, params::eqQIDs[band], 0.9f);
    }
}

void applyEQPreset (juce::AudioProcessorValueTreeState& apvts, ToneStack::EQPreset preset)
{
  const auto settings = ToneStack::makeEQPreset (preset);
  auto set = [&apvts] (const char* id, float value)
  {
    if (id == nullptr)
      return;
    if (auto* parameter = apvts.getParameter (id))
    {
      parameter->beginChangeGesture();
      parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
      parameter->endChangeGesture();
    }
  };
  for (size_t band = 0; band < settings.size(); ++band)
  {
    set (params::eqTypeIDs[band], (float) (int) settings[band].type);
    set (params::eqGainIDs[band], settings[band].gainDb);
    set (params::eqFrequencyIDs[band], settings[band].frequency);
    set (params::eqQIDs[band], settings[band].q);
  }
}

} // namespace drumbreaker
