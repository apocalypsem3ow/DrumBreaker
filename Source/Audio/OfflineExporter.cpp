#include "OfflineExporter.h"

#include <cmath>
#include <limits>

namespace drumbreaker
{

juce::Result OfflineExporter::writeWav (const Snapshot& snapshot, const juce::File& destination,
                                       const std::function<bool()>& cancelled)
{
    if (! snapshot.license.isVerified())
        return juce::Result::fail ("Activate and verify DrumBreaker before exporting audio.");
    const auto& data = snapshot.data;
    if (data == nullptr || data->audio == nullptr || data->audio->getNumSamples() < 2
        || data->totalLength <= 0 || data->arrangement.empty())
        return juce::Result::fail ("Load a sample with a non-empty arrangement before exporting.");
    if (! std::isfinite (snapshot.sampleRate) || snapshot.sampleRate < 8000.0 || snapshot.sampleRate > 384000.0
        || ! std::isfinite (snapshot.pitchSemitones) || ! std::isfinite (data->sourceSampleRate)
        || data->sourceSampleRate <= 0.0)
        return juce::Result::fail ("The export sample rate or pitch is invalid.");
    for (int index : data->arrangement)
    {
        if (! juce::isPositiveAndBelow (index, (int) data->slices.size()))
            return juce::Result::fail ("The arrangement contains an invalid slice.");
        const auto& slice = data->slices[(size_t) index];
        if (slice.start < 0 || slice.end > data->audio->getNumSamples() || slice.end <= slice.start)
            return juce::Result::fail ("The arrangement contains an invalid slice range.");
    }
    if (destination.exists())
        return juce::Result::fail ("An export already exists at this path; choose a new filename.");
    if (! destination.getParentDirectory().isDirectory())
        return juce::Result::fail ("The selected export folder does not exist.");

    const double pitchRatio = std::pow (2.0, snapshot.pitchSemitones / 12.0);
    const double duration = (double) data->totalLength / data->sourceSampleRate / pitchRatio;
    double tail = snapshot.reverb > 0.0f ? 4.0 : 0.0;
    const bool centredFilter = ToneStack::isFilterPadCentred (snapshot.filter.cutoff, snapshot.filter.resonance)
        && snapshot.modulation == 0.0f && snapshot.motion == ToneStack::FilterMotion::none;
    if (snapshot.filterEnabled && snapshot.filterMix > 0.0f && ! centredFilter)
    {
        const double cutoff = snapshot.motion == ToneStack::FilterMotion::lowPassSweep
                              || snapshot.motion == ToneStack::FilterMotion::highPassSweep
                              || snapshot.motion == ToneStack::FilterMotion::bandPassSweep
            ? 20.0 : juce::jmax (20.0, (double) snapshot.filter.cutoff);
        const double resonance = snapshot.motion == ToneStack::FilterMotion::resonanceModulation
            ? 3.5 : snapshot.filter.resonance;
        tail = juce::jmax (tail, juce::jlimit (0.02, 4.0,
            0.02 + 12.0 * resonance / (juce::MathConstants<double>::pi * cutoff)));
    }
    if (snapshot.eqEnabled)
        for (const auto& band : snapshot.eq)
            if (band.gainDb != 0.0f || band.type == ToneStack::EQType::lowPass || band.type == ToneStack::EQType::highPass)
                tail = juce::jmax (tail, juce::jlimit (0.02, 4.0,
                    12.0 * band.q / (juce::MathConstants<double>::pi * juce::jmax (20.0, (double) band.frequency))));
    const double frames = std::ceil (duration * snapshot.sampleRate) + std::ceil (tail * snapshot.sampleRate);
    if (! std::isfinite (frames) || frames <= 0.0 || frames > 3600.0 * snapshot.sampleRate)
        return juce::Result::fail ("This arrangement exceeds the one-hour export limit.");
    const int channels = juce::jlimit (1, 2, snapshot.channels);
    if (frames * channels * sizeof (float) > (double) std::numeric_limits<juce::uint32>::max() - 65536.0)
        return juce::Result::fail ("This export is too large for a standard WAV file.");
    if (cancelled && cancelled())
        return juce::Result::fail ("Export cancelled.");

    juce::TemporaryFile temporary (destination);
    auto stream = temporary.getFile().createOutputStream();
    if (stream == nullptr || stream->failedToOpen())
        return juce::Result::fail ("Cannot write to the selected export folder.");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
        stream.get(), snapshot.sampleRate, (unsigned int) channels, 32, {}, 0));
    if (writer == nullptr)
        return juce::Result::fail ("Cannot create the WAV writer.");
    stream.release();

    constexpr int blockSize = 512;
    BreakEngine engine;
    engine.prepare (snapshot.sampleRate, blockSize);
    engine.setLooping (false);
    engine.setData (data->withArrangement (data->arrangement));
    engine.setPitchOffsetSemitones (snapshot.pitchSemitones);
    engine.noteOn (60, 1.0f);
    ToneStack effects;
    effects.prepare ({ snapshot.sampleRate, blockSize, (juce::uint32) channels });
    effects.setEQ (snapshot.eq);
    effects.setEQEnabled (snapshot.eqEnabled);
    effects.setFilter (snapshot.filter.type, snapshot.filter.cutoff, snapshot.filter.resonance,
                        snapshot.filterDrive, snapshot.filterMix, snapshot.modulation);
    effects.setFilterEnabled (snapshot.filterEnabled);
    effects.setFilterMotion (snapshot.motion);
    effects.setReverb (snapshot.reverb);
    effects.setFilterPlayback (duration, true, true);
    effects.snapParametersToTargets();
    const float outputGain = juce::Decibels::decibelsToGain (snapshot.outputDb);
    juce::AudioBuffer<float> block (channels, blockSize);
    const juce::int64 bodyFrames = (juce::int64) std::ceil (duration * snapshot.sampleRate);
    const juce::int64 totalFrames = (juce::int64) frames;

    for (juce::int64 position = 0; position < totalFrames;)
    {
        if (! snapshot.license.isVerified())
            return juce::Result::fail ("License verification was lost; the unfinished export was discarded.");
        if (cancelled && cancelled())
            return juce::Result::fail ("Export cancelled.");
        const int count = (int) juce::jmin<juce::int64> (blockSize, totalFrames - position);
        block.setSize (channels, count, false, false, true);
        block.clear();
        engine.renderNextBlock (block, 0, count);
        effects.setFilterPlayback (duration, position < bodyFrames, false);
        effects.process (block);
        block.applyGain (outputGain);
        for (int channel = 0; channel < channels; ++channel)
            for (int sample = 0; sample < count; ++sample)
                if (! std::isfinite (block.getSample (channel, sample)))
                    return juce::Result::fail ("The rendered audio contains invalid values.");
        if (! writer->writeFromAudioSampleBuffer (block, 0, count))
            return juce::Result::fail ("Writing the WAV failed; check free disk space and folder permissions.");
        position += count;
    }
    writer.reset();
    if (! snapshot.license.isVerified())
        return juce::Result::fail ("License verification was lost; the unfinished export was discarded.");
    if (cancelled && cancelled())
        return juce::Result::fail ("Export cancelled.");
    if (destination.exists())
        return juce::Result::fail ("Another file appeared at the export destination; choose a new filename.");
    if (! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Cannot finish saving the WAV file.");
    return juce::Result::ok();
}

} // namespace drumbreaker