#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace drumbreaker
{

/** One detected fragment of the loaded sample. */
struct Slice
{
    juce::int64 start = 0;  // inclusive, in source samples
    juce::int64 end   = 0;  // exclusive, in source samples

    juce::int64 length() const noexcept { return juce::jmax<juce::int64> (0, end - start); }
};

struct AnalysisResult
{
    double bpm = 0.0;
    std::vector<Slice> slices;
};

/** Maximum accepted sample length, in seconds. */
constexpr double kMaxSampleSeconds = 60.0;

/** Transient / tempo analysis for drum loops. All methods are offline & thread safe. */
class SampleAnalyzer
{
public:
    struct Settings
    {
        float sensitivity   = 0.55f; // 0 = few slices, 1 = many slices
        double minSliceMs   = 35.0;  // refractory period between transients
    };

    /** Detects the tempo of a loop. Returns 0 when nothing usable was found. */
    static double detectTempo (const juce::AudioBuffer<float>& buffer, double sampleRate);

    /** Detects transients and returns contiguous slices covering the whole buffer. */
    static std::vector<Slice> detectSlices (const juce::AudioBuffer<float>& buffer,
                                            double sampleRate,
                                            const Settings& settings);

    static AnalysisResult analyse (const juce::AudioBuffer<float>& buffer,
                                   double sampleRate,
                                   const Settings& settings);

private:
    /** Spectral-flux onset envelope, one value per hop. */
    static std::vector<float> onsetEnvelope (const juce::AudioBuffer<float>& buffer,
                                             int fftOrder, int hopSize);
};

} // namespace drumbreaker
