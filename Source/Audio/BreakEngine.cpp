#include "BreakEngine.h"

#include <cmath>

namespace drumbreaker
{

void BreakEngine::prepare (double hostSampleRate, int)
{
    sampleRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    reset();
}

void BreakEngine::reset()
{
    voice = Voice();
    releasingVoice = Voice();
    playing.store (false);
    playhead.store (0.0f);
    restartRequested.store (false);

    const juce::ScopedLock sl (noteLock);
    heldNotes.clearQuick();
}

void BreakEngine::setData (BreakData::Ptr newData, bool restartPlayback)
{
    if (newData != nullptr)
        newData->rebuildTimeline();

    const juce::SpinLock::ScopedLockType sl (dataLock);
    data = newData;
    if (restartPlayback)
        restartRequested.store (true);
}

BreakData::Ptr BreakEngine::getData() const
{
    const juce::SpinLock::ScopedLockType sl (dataLock);
    return data;
}

void BreakEngine::startVoice (Voice& v, int note, float velocity)
{
    v.active   = true;
    v.note     = note;
    v.velocity = velocity;
    v.position = 0.0;
    v.entry    = 0;
    v.stage    = DefaultEnvelope::attackMs > 0.0 ? Stage::attack : Stage::hold;
    v.level    = DefaultEnvelope::attackMs > 0.0 ? 0.0f : 1.0f;
    v.stageSamplesLeft = (DefaultEnvelope::attackMs > 0.0 ? DefaultEnvelope::attackMs
                                                          : DefaultEnvelope::holdMs)
                            * 0.001 * sampleRate;
    v.fadeOutGain = 1.0f;
    v.fadeOutStep = 0.0f;
}

void BreakEngine::noteOn (int midiNote, float velocity)
{
    {
        const juce::ScopedLock sl (noteLock);
        heldNotes.removeAllInstancesOf (midiNote);
        heldNotes.add (midiNote);
    }

    // Mono "cut itself": the outgoing note is faded out over a few milliseconds
    // instead of being hard-stopped, so stacked notes never clash or click.
    if (voice.active)
    {
        releasingVoice = voice;
        releasingVoice.fadeOutStep = 1.0f / (float) juce::jmax (1.0, kRetriggerFadeMs * 0.001 * sampleRate);
    }

    startVoice (voice, midiNote, velocity);
    playing.store (true);
}

void BreakEngine::noteOff (int midiNote)
{
    int fallback = -1;

    {
        const juce::ScopedLock sl (noteLock);
        heldNotes.removeAllInstancesOf (midiNote);

        if (! heldNotes.isEmpty())
            fallback = heldNotes.getLast();
    }

    if (voice.note != midiNote)
        return; // an older, already-cut note was released: nothing to do

    if (fallback >= 0)
    {
        // A previously held note is still down: hand playback back to it.
        releasingVoice = voice;
        releasingVoice.fadeOutStep = 1.0f / (float) juce::jmax (1.0, kRetriggerFadeMs * 0.001 * sampleRate);
        startVoice (voice, fallback, voice.velocity);
        return;
    }

    voice.stage = Stage::release;
    voice.stageSamplesLeft = DefaultEnvelope::releaseMs * 0.001 * sampleRate;
}

void BreakEngine::allNotesOff()
{
    {
        const juce::ScopedLock sl (noteLock);
        heldNotes.clearQuick();
    }

    if (voice.active)
    {
        voice.stage = Stage::release;
        voice.stageSamplesLeft = DefaultEnvelope::releaseMs * 0.001 * sampleRate;
    }
}

void BreakEngine::advanceEnvelope (Voice& v)
{
    switch (v.stage)
    {
        case Stage::attack:
        {
            const double total = juce::jmax (1.0, DefaultEnvelope::attackMs * 0.001 * sampleRate);
            v.level = juce::jlimit (0.0f, 1.0f, v.level + (float) (1.0 / total));

            if (--v.stageSamplesLeft <= 0.0)
            {
                v.level = 1.0f;
                v.stage = Stage::hold;
                v.stageSamplesLeft = DefaultEnvelope::holdMs * 0.001 * sampleRate;
            }

            break;
        }

        case Stage::hold:
        {
            v.level = 1.0f;
            break;
        }

        case Stage::decay:
        {
            const double total = juce::jmax (1.0, DefaultEnvelope::decayMs * 0.001 * sampleRate);
            v.level = juce::jmax (DefaultEnvelope::sustain,
                                  v.level - (float) ((1.0 - DefaultEnvelope::sustain) / total));

            if (--v.stageSamplesLeft <= 0.0)
            {
                v.level = DefaultEnvelope::sustain;
                v.stage = Stage::sustain;
            }

            break;
        }

        case Stage::sustain:
            v.level = DefaultEnvelope::sustain;
            break;

        case Stage::release:
        {
            const double total = juce::jmax (1.0, DefaultEnvelope::releaseMs * 0.001 * sampleRate);
            v.level = juce::jmax (0.0f, v.level - (float) (1.0 / total));

            if (--v.stageSamplesLeft <= 0.0 || v.level <= 0.0f)
            {
                v.level  = 0.0f;
                v.stage  = Stage::idle;
                v.active = false;
            }

            break;
        }

        case Stage::idle:
        default:
            v.level  = 0.0f;
            v.active = false;
            break;
    }
}

float BreakEngine::readSample (const BreakData& d, const Voice& v, int channel) const
{
    if (v.entry >= d.starts.size())
        return 0.0f;

    const int sliceIndex = d.arrangement[v.entry];

    if (! juce::isPositiveAndBelow (sliceIndex, (int) d.slices.size()))
        return 0.0f;

    const auto& slice = d.slices[(size_t) sliceIndex];
    const double offsetInSlice = v.position - (double) d.starts[v.entry];
    const double sourcePos = (double) slice.start + offsetInSlice;

    const auto& audio = *d.audio;
    const int maxIndex = audio.getNumSamples() - 1;

    if (maxIndex < 0 || sourcePos < 0.0 || sourcePos >= (double) maxIndex)
        return 0.0f;

    const int   i0    = (int) sourcePos;
    const float frac  = (float) (sourcePos - (double) i0);
    const int   ch    = juce::jmin (channel, audio.getNumChannels() - 1);
    const float* read = audio.getReadPointer (ch);

    float value = read[i0] + frac * (read[i0 + 1] - read[i0]);

    // Short fades at the slice edges keep re-ordered fragments click free.
    const double fadeLen = juce::jmax (1.0, kSliceEdgeFadeMs * 0.001 * d.sourceSampleRate);
    const double len     = (double) slice.length();
    const double inGain  = juce::jlimit (0.0, 1.0, offsetInSlice / fadeLen);
    const double outGain = juce::jlimit (0.0, 1.0, (len - offsetInSlice) / fadeLen);

    return value * (float) juce::jmin (inGain, outGain);
}

void BreakEngine::renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    const juce::SpinLock::ScopedTryLockType sl (dataLock);

    if (! sl.isLocked())
        return;

    if (restartRequested.exchange (false))
    {
        voice.position = 0.0;
        voice.entry = 0;
        releasingVoice.active = false;
        playhead.store (0.0f);
    }

    if (data == nullptr || data->audio == nullptr || data->totalLength <= 0)
    {
        playing.store (false);
        return;
    }

    const BreakData& d = *data;
    const int numChannels = output.getNumChannels();

    const double baseRatio = d.sourceSampleRate / sampleRate;
    const float  offset    = pitchOffset.load();
    playbackPitchRatio.store (voice.active ? std::pow (2.0, ((double) voice.note - 60.0 + offset) / 12.0) : 1.0);

    Voice* voices[] = { &voice, &releasingVoice };

    for (Voice* vp : voices)
    {
        Voice& v = *vp;

        if (! v.active)
            continue;

        const double increment = baseRatio
            * std::pow (2.0, ((double) v.note - 60.0 + (double) offset) / 12.0);

        for (int i = 0; i < numSamples; ++i)
        {
            if (! v.active)
                break;

            if (v.position >= (double) d.totalLength)
            {
                if (looping && vp == &voice && v.stage != Stage::release && v.fadeOutStep <= 0.0f)
                {
                    v.position = std::fmod (v.position, (double) d.totalLength);
                    v.entry = 0;
                }
                else
                {
                    v.active = false;
                    break;
                }
            }

            while (v.entry + 1 < d.starts.size() && v.position >= (double) d.starts[v.entry + 1])
                ++v.entry;

            const float gain = v.level * v.velocity * v.fadeOutGain;

            for (int ch = 0; ch < numChannels; ++ch)
                output.addSample (ch, startSample + i, readSample (d, v, ch) * gain);

            v.position += increment;
            advanceEnvelope (v);

            if (v.fadeOutStep > 0.0f)
            {
                v.fadeOutGain -= v.fadeOutStep;

                if (v.fadeOutGain <= 0.0f)
                {
                    v.fadeOutGain = 0.0f;
                    v.active = false;
                }
            }
        }
    }

    if (voice.active && looping && voice.stage != Stage::release
        && voice.position >= (double) d.totalLength)
    {
        voice.position = std::fmod (voice.position, (double) d.totalLength);
        voice.entry = 0;
    }
    playing.store (voice.active);
    playhead.store (voice.active ? (float) juce::jlimit (0.0, 1.0, voice.position / (double) d.totalLength)
                                 : 0.0f);
}

} // namespace drumbreaker
