#pragma once

#include "Analysis/SampleAnalyzer.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

namespace drumbreaker
{

/** Immutable snapshot of "what is loaded and how it is arranged".
    Built on the message thread, published to the audio thread as a whole.
*/
class BreakData : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<BreakData>;

    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double sourceSampleRate = 44100.0;
    juce::String name;

    std::vector<Slice> slices;      // source regions, in the order they were detected
    std::vector<int>   arrangement; // indices into slices, in playback order

    // Derived: output start position of each arrangement entry (source-rate samples).
    std::vector<juce::int64> starts;
    juce::int64 totalLength = 0;

    /** Cheap copy that shares the audio but uses a different playback order. */
    Ptr withArrangement (const std::vector<int>& newArrangement) const
    {
        Ptr copy = new BreakData();
        copy->audio            = audio;
        copy->sourceSampleRate = sourceSampleRate;
        copy->name             = name;
        copy->slices           = slices;
        copy->arrangement      = newArrangement;
        copy->rebuildTimeline();
        return copy;
    }

    void rebuildTimeline()
    {
        starts.clear();
        starts.reserve (arrangement.size());
        totalLength = 0;

        for (int index : arrangement)
        {
            if (! juce::isPositiveAndBelow (index, (int) slices.size()))
                continue;

            starts.push_back (totalLength);
            totalLength += slices[(size_t) index].length();
        }
    }
};

/** The default, non-editable amp envelope described in the product spec:
    attack 0, hold 100%, decay 0%, sustain 3%, release 4%.
    Hold and release percentages are of their respective maximum times.
*/
struct DefaultEnvelope
{
    static constexpr double maxHoldMs    = 2000.0;
    static constexpr double maxDecayMs   = 2000.0;
    static constexpr double maxReleaseMs = 500.0;

    static constexpr double attackMs  = 0.0;
    static constexpr double holdMs    = maxHoldMs * 1.00;   // 100 %
    static constexpr double decayMs   = maxDecayMs * 0.00;  //   0 %
    static constexpr float  sustain   = 0.03f;              //   3 %
    static constexpr double releaseMs = maxReleaseMs * 0.04;//   4 %
};

/** Plays the arranged break back, monophonically, with last-note priority. */
class BreakEngine
{
public:
    BreakEngine() = default;

    void prepare (double hostSampleRate, int maximumBlockSize);
    void reset();

    /** Publishes a new snapshot. Safe to call from the message thread. */
    void setData (BreakData::Ptr newData);
    BreakData::Ptr getData() const;

    void noteOn (int midiNote, float velocity);
    void noteOff (int midiNote);
    void allNotesOff();
    void setLooping (bool enabled) noexcept { looping = enabled; }

    /** Semitone offset applied on top of the incoming note (pitch knob). */
    void setPitchOffsetSemitones (float semitones) noexcept { pitchOffset.store (semitones); }

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples);

    bool isPlaying() const noexcept { return playing.load(); }

    /** 0 .. 1 position through the arrangement, for the UI playhead. */
    float getPlayheadNormalised() const noexcept { return playhead.load(); }
    double getPlaybackPitchRatio() const noexcept { return playbackPitchRatio.load(); }

private:
    enum class Stage { idle, attack, hold, decay, sustain, release };

    struct Voice
    {
        bool   active   = false;
        int    note     = -1;
        float  velocity = 1.0f;
        double position = 0.0;   // in arrangement samples, at source rate
        size_t entry    = 0;     // cached arrangement index

        Stage  stage    = Stage::idle;
        float  level    = 0.0f;
        double stageSamplesLeft = 0.0;

        float  fadeOutGain = 1.0f;
        float  fadeOutStep = 0.0f;
    };

    void startVoice (Voice& v, int note, float velocity);
    void advanceEnvelope (Voice& v);
    float readSample (const BreakData& data, const Voice& v, int channel) const;

    double sampleRate = 44100.0;
    bool looping = true;

    mutable juce::SpinLock dataLock;
    BreakData::Ptr data;

    Voice voice;
    Voice releasingVoice; // tail of the previous note, so re-triggers never click

    juce::Array<int> heldNotes;
    juce::CriticalSection noteLock;

    std::atomic<float> pitchOffset { 0.0f };
    std::atomic<bool>  playing     { false };
    std::atomic<float> playhead    { 0.0f };
    std::atomic<double> playbackPitchRatio { 1.0 };

    static constexpr double kRetriggerFadeMs = 4.0;
    static constexpr double kSliceEdgeFadeMs = 1.5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakEngine)
};

} // namespace drumbreaker
