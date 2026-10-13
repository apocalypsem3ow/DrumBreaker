#pragma once

#include <array>
#include <cmath>

namespace drumbreaker::params
{

inline constexpr const char* pitch        = "pitch";
inline constexpr const char* outputGain   = "outputGain";
inline constexpr const char* reverb       = "reverb";
inline constexpr const char* eqEnabled    = "eqEnabled";

inline constexpr std::array<const char*, 7> eqTypeIDs
	{ "eqBand1Type", "eqBand2Type", "eqBand3Type", "eqBand4Type", "eqBand5Type", "eqBand6Type", "eqBand7Type" };

inline constexpr const char* eqLowGain    = "eqLowGain";
inline constexpr const char* eqLowFreq    = "eqLowFreq";
inline constexpr const char* eqMidGain    = "eqMidGain";
inline constexpr const char* eqMidFreq    = "eqMidFreq";
inline constexpr const char* eqMidQ       = "eqMidQ";
inline constexpr const char* eqHighGain   = "eqHighGain";
inline constexpr const char* eqHighFreq   = "eqHighFreq";

inline constexpr std::array<const char*, 7> eqGainIDs
	{ eqLowGain, "eqBand2Gain", "eqBand3Gain", eqMidGain, "eqBand5Gain", "eqBand6Gain", eqHighGain };
inline constexpr std::array<const char*, 7> eqFrequencyIDs
	{ eqLowFreq, "eqBand2Freq", "eqBand3Freq", eqMidFreq, "eqBand5Freq", "eqBand6Freq", eqHighFreq };
inline constexpr std::array<const char*, 7> eqQIDs
	{ nullptr, "eqBand2Q", "eqBand3Q", eqMidQ, "eqBand5Q", "eqBand6Q", nullptr };

inline constexpr float eqMinFrequency = 20.0f;
inline constexpr float eqMaxFrequency = 20000.0f;

inline float eqFrequencyAtPosition (float position)
{
	return eqMinFrequency * std::pow (eqMaxFrequency / eqMinFrequency, position);
}

inline float eqPositionAtFrequency (float frequency)
{
	return std::log (frequency / eqMinFrequency) / std::log (eqMaxFrequency / eqMinFrequency);
}

inline float eqDefaultFrequency (size_t band)
{
	return eqFrequencyAtPosition ((float) (band + 1) / 8.0f);
}

inline constexpr const char* filterType   = "filterType";
inline constexpr const char* filterCutoff = "filterCutoff";
inline constexpr const char* filterReso   = "filterReso";
inline constexpr const char* filterDrive  = "filterDrive";
inline constexpr const char* filterMix    = "filterMix";
inline constexpr const char* filterPreset = "filterPreset";
inline constexpr const char* filterEnabled = "filterEnabled";
inline constexpr const char* filterMotion = "filterMotion";

inline constexpr const char* sensitivity  = "sensitivity";

} // namespace drumbreaker::params
