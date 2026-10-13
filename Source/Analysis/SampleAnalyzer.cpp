#include "SampleAnalyzer.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace drumbreaker
{

static constexpr int kFftOrder = 10;             // 1024
static constexpr int kHopSize  = 256;

std::vector<float> SampleAnalyzer::onsetEnvelope (const juce::AudioBuffer<float>& buffer,
                                                  int fftOrder, int hopSize)
{
    const int fftSize  = 1 << fftOrder;
    const int numBins  = fftSize / 2;
    const int numSamps = buffer.getNumSamples();
    const int numChans = buffer.getNumChannels();

    std::vector<float> envelope;

    if (numSamps < fftSize || numChans <= 0)
        return envelope;

    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                                juce::dsp::WindowingFunction<float>::hann,
                                                false);

    std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
    std::vector<float> previous ((size_t) numBins, 0.0f);

    const float chanScale = 1.0f / (float) numChans;
    envelope.reserve ((size_t) (numSamps / hopSize + 1));

    for (int pos = 0; pos + fftSize <= numSamps; pos += hopSize)
    {
        std::fill (fftData.begin(), fftData.end(), 0.0f);

        for (int ch = 0; ch < numChans; ++ch)
        {
            const float* src = buffer.getReadPointer (ch, pos);

            for (int i = 0; i < fftSize; ++i)
                fftData[(size_t) i] += src[i] * chanScale;
        }

        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        float flux = 0.0f;

        for (int bin = 0; bin < numBins; ++bin)
        {
            // Log compression keeps quiet hi-hats visible next to loud kicks.
            const float mag  = std::log1p (100.0f * fftData[(size_t) bin]);
            const float diff = mag - previous[(size_t) bin];

            if (diff > 0.0f)
                flux += diff;

            previous[(size_t) bin] = mag;
        }

        envelope.push_back (flux);
    }

    return envelope;
}

double SampleAnalyzer::detectTempo (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (sampleRate <= 0.0 || buffer.getNumSamples() <= 0)
        return 0.0;

    auto env = onsetEnvelope (buffer, kFftOrder, kHopSize);

    if (env.size() < 16)
        return 0.0;

    // Remove the DC / slow-moving component so autocorrelation sees only the pulse.
    const float mean = std::accumulate (env.begin(), env.end(), 0.0f) / (float) env.size();

    for (auto& v : env)
        v = juce::jmax (0.0f, v - mean);

    const double hopRate = sampleRate / (double) kHopSize; // envelope frames per second

    const int minLag = juce::jmax (1, (int) std::floor (hopRate * 60.0 / 200.0));
    const int maxLag = juce::jmin ((int) env.size() - 1, (int) std::ceil (hopRate * 60.0 / 60.0));

    if (maxLag <= minLag)
        return 0.0;

    double bestScore = 0.0;
    int    bestLag   = 0;
    std::vector<double> scores ((size_t) maxLag + 1, 0.0);

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        const int n = (int) env.size() - lag;

        for (int i = 0; i < n; ++i)
            sum += (double) env[(size_t) i] * (double) env[(size_t) (i + lag)];

        // Reinforce lags whose multiples also correlate: picks the beat, not a sub-division.
        double harmonic = 0.0;

        for (int mult = 2; mult <= 4; ++mult)
        {
            const int l = lag * mult;

            if (l >= (int) env.size())
                break;

            double s = 0.0;
            const int m = (int) env.size() - l;

            for (int i = 0; i < m; ++i)
                s += (double) env[(size_t) i] * (double) env[(size_t) (i + l)];

            harmonic += s / (double) juce::jmax (1, m);
        }

        const double score = sum / (double) juce::jmax (1, n) + 0.5 * harmonic;
        scores[(size_t) lag] = score;

        if (score > bestScore)
        {
            bestScore = score;
            bestLag   = lag;
        }
    }

    if (bestLag <= 0)
        return 0.0;

    double refinedLag = (double) bestLag;

    if (bestLag > minLag && bestLag < maxLag)
    {
        const double before = scores[(size_t) bestLag - 1];
        const double centre = scores[(size_t) bestLag];
        const double after  = scores[(size_t) bestLag + 1];
        const double curvature = before - 2.0 * centre + after;

        if (curvature < 0.0)
            refinedLag += juce::jlimit (-0.5, 0.5, 0.5 * (before - after) / curvature);
    }

    double bpm = 60.0 * hopRate / refinedLag;

    while (bpm < 70.0)  bpm *= 2.0;
    while (bpm > 190.0) bpm *= 0.5;

    struct Onset
    {
        double frame;
        double weight;
    };
    std::vector<Onset> onsets;
    const float maximum = *std::max_element (env.begin(), env.end());
    for (size_t frame = 1; frame + 1 < env.size(); ++frame)
    {
        const double before = env[frame - 1];
        const double centre = env[frame];
        const double after = env[frame + 1];
        if (centre < maximum * 0.10 || centre <= before || centre < after)
            continue;
        const double curvature = before - 2.0 * centre + after;
        const double offset = curvature < 0.0
            ? juce::jlimit (-0.5, 0.5, 0.5 * (before - after) / curvature) : 0.0;
        onsets.push_back ({ (double) frame + offset, std::sqrt (centre) });
    }

    struct GridFit
    {
        double tempo = 0.0;
        double score = 0.0;
    };
    auto fitGrid = [&onsets, hopRate] (double candidate)
    {
        GridFit result;
        if (onsets.size() < 6)
            return result;
        const double period = hopRate * 60.0 / (candidate * 4.0);
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        double real = 0.0, imaginary = 0.0, totalWeight = 0.0;
        double eighthReal = 0.0, eighthImaginary = 0.0;
        double beatReal = 0.0, beatImaginary = 0.0;
        for (const auto& onset : onsets)
        {
            const double angle = twoPi * onset.frame / period;
            real += onset.weight * std::cos (angle);
            imaginary += onset.weight * std::sin (angle);
            eighthReal += onset.weight * std::cos (angle * 0.5);
            eighthImaginary += onset.weight * std::sin (angle * 0.5);
            beatReal += onset.weight * std::cos (angle * 0.25);
            beatImaginary += onset.weight * std::sin (angle * 0.25);
            totalWeight += onset.weight;
        }
        const double phase = std::atan2 (imaginary, real) * period / twoPi;
        const double coherence = std::hypot (real, imaginary) / totalWeight;
        double weight = 0.0, sumTick = 0.0, sumFrame = 0.0, sumSquare = 0.0, sumProduct = 0.0;
        int accepted = 0;
        for (const auto& onset : onsets)
        {
            const double tick = std::round ((onset.frame - phase) / period);
            if (std::abs (onset.frame - phase - tick * period) > period * 0.18)
                continue;
            weight += onset.weight;
            sumTick += onset.weight * tick;
            sumFrame += onset.weight * onset.frame;
            sumSquare += onset.weight * tick * tick;
            sumProduct += onset.weight * tick * onset.frame;
            ++accepted;
        }
        const double denominator = weight * sumSquare - sumTick * sumTick;
        if (accepted < 6 || weight < totalWeight * 0.55 || denominator <= 0.0)
            return result;
        const double slope = (weight * sumProduct - sumTick * sumFrame) / denominator;
        if (slope <= 0.0)
            return result;
        result.tempo = 60.0 * hopRate / (4.0 * slope);
        const double eighthCoherence = std::hypot (eighthReal, eighthImaginary) / totalWeight;
        const double beatCoherence = std::hypot (beatReal, beatImaginary) / totalWeight;
        result.score = (coherence + 0.15 * eighthCoherence + 0.10 * beatCoherence) * weight / totalWeight;
        return result;
    };

    const double seconds = (double) buffer.getNumSamples() / sampleRate;
    GridFit bestLoop;
    for (int beats : { 2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192 })
    {
        const double candidate = 60.0 * beats / seconds;
        if (candidate < 70.0 || candidate > 200.0)
            continue;
        const auto fitted = fitGrid (candidate);
        if (fitted.score < 0.55 || std::abs (fitted.tempo - candidate) > candidate * 0.003)
            continue;
        const double score = fitted.score * (candidate >= 100.0 ? 1.04 : 1.0);
        if (score > bestLoop.score)
            bestLoop = { candidate, score };
    }
    if (bestLoop.tempo > 0.0)
        return bestLoop.tempo;

    GridFit bestPulse;
    for (double ratio : { 1.0, 2.0, 0.5, 1.5, 2.0 / 3.0 })
    {
        const double candidate = bpm * ratio;
        if (candidate < 70.0 || candidate > 200.0)
            continue;
        const auto fitted = fitGrid (candidate);
        if (fitted.score < 0.55 || std::abs (fitted.tempo - candidate) > candidate * 0.02)
            continue;
        if (fitted.score > bestPulse.score)
            bestPulse = fitted;
    }
    if (bestPulse.tempo > 0.0)
        return bestPulse.tempo;
    return bpm;
}

std::vector<Slice> SampleAnalyzer::detectSlices (const juce::AudioBuffer<float>& buffer,
                                                 double sampleRate,
                                                 const Settings& settings)
{
    std::vector<Slice> slices;

    const juce::int64 numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || sampleRate <= 0.0)
        return slices;

    auto env = onsetEnvelope (buffer, kFftOrder, kHopSize);

    if (env.empty())
    {
        slices.push_back ({ 0, numSamples });
        return slices;
    }

    // Adaptive threshold: local median-ish mean over ~200 ms plus a global floor.
    const int    localFrames = juce::jmax (4, (int) ((0.2 * sampleRate) / kHopSize));
    const float  peak        = *std::max_element (env.begin(), env.end());

    if (peak <= 0.0f)
    {
        slices.push_back ({ 0, numSamples });
        return slices;
    }

    const float sensitivity = juce::jlimit (0.0f, 1.0f, settings.sensitivity);
    const float multiplier  = juce::jmap (sensitivity, 2.2f, 1.02f);
    const float floorLevel  = juce::jmap (sensitivity, 0.18f, 0.01f) * peak;

    const int refractory = juce::jmax (1, (int) ((settings.minSliceMs * 0.001 * sampleRate) / kHopSize));

    std::vector<int> onsetFrames;
    int lastOnset = -refractory;

    for (int i = 1; i + 1 < (int) env.size(); ++i)
    {
        const float value = env[(size_t) i];

        if (value < env[(size_t) (i - 1)] || value < env[(size_t) (i + 1)])
            continue; // not a local maximum

        const int from = juce::jmax (0, i - localFrames);
        const int to   = juce::jmin ((int) env.size(), i + localFrames / 4);

        float localMean = 0.0f;

        for (int j = from; j < to; ++j)
            localMean += env[(size_t) j];

        localMean /= (float) juce::jmax (1, to - from);

        if (value < localMean * multiplier || value < floorLevel)
            continue;

        if (i - lastOnset < refractory)
        {
            if (! onsetFrames.empty() && value > env[(size_t) onsetFrames.back()])
            {
                onsetFrames.back() = i; // keep the stronger of two adjacent peaks
                lastOnset = i;
            }

            continue;
        }

        onsetFrames.push_back (i);
        lastOnset = i;
    }

    // Convert frames to sample positions and back-track to the nearest pre-transient dip
    // so the attack is never clipped off the front of a slice.
    const juce::int64 backtrack = (juce::int64) (0.012 * sampleRate);

    std::vector<juce::int64> cuts;
    cuts.push_back (0);

    for (int frame : onsetFrames)
    {
        juce::int64 pos = (juce::int64) frame * kHopSize;
        const juce::int64 searchFrom = juce::jmax<juce::int64> (0, pos - backtrack);

        float lowest = std::numeric_limits<float>::max();
        juce::int64 bestPos = searchFrom;

        for (juce::int64 s = searchFrom; s < juce::jmin (pos, numSamples); ++s)
        {
            float magnitude = 0.0f;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                magnitude += std::abs (buffer.getReadPointer (ch)[s]);

            if (magnitude < lowest)
            {
                lowest  = magnitude;
                bestPos = s;
            }
        }

        pos = bestPos;

        if (pos - cuts.back() >= (juce::int64) (settings.minSliceMs * 0.001 * sampleRate))
            cuts.push_back (pos);
    }

    for (size_t i = 0; i < cuts.size(); ++i)
    {
        const juce::int64 start = cuts[i];
        const juce::int64 end   = (i + 1 < cuts.size()) ? cuts[i + 1] : numSamples;

        if (end > start)
            slices.push_back ({ start, end });
    }

    if (slices.empty())
        slices.push_back ({ 0, numSamples });

    return slices;
}

AnalysisResult SampleAnalyzer::analyse (const juce::AudioBuffer<float>& buffer,
                                        double sampleRate,
                                        const Settings& settings)
{
    AnalysisResult result;
    result.bpm    = detectTempo (buffer, sampleRate);
    result.slices = detectSlices (buffer, sampleRate, settings);
    return result;
}

} // namespace drumbreaker
