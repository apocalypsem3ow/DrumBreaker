#pragma once

#include "Analysis/SampleAnalyzer.h"
#include "Audio/BreakEngine.h"
#include "Audio/ToneStack.h"
#include "Audio/OfflineExporter.h"
#include "Licensing/LicenseClient.h"
#include "UI/SpectrumDisplay.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace drumbreaker
{

class DrumBreakerProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit DrumBreakerProcessor (licensing::LicenseClient::Options licensingOptions = {});
    ~DrumBreakerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DrumBreaker"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    /** Loads an audio file (max 60 s). Returns false and fills errorMessage on failure. */
    bool loadSampleFile (const juce::File& file, juce::String& errorMessage);

    /** Runs transient detection and replaces the current arrangement. */
    void breakSample();

    /** Restores the original sample to one whole fragment without changing the custom break. */
    void restoreWholeSample();

    void resetToDefaults();

    /** Replaces the playback order (called by the waveform editor after a drag). */
    void setArrangement (const std::vector<int>& newArrangement);
    void setCustomArrangement (const std::vector<int>& newArrangement);
    bool createCustomBreak();
    bool hasCustomBreak() const;
    bool hasBrokenSample() const noexcept { return sampleBroken.load(); }
    void selectCustomBreak (bool selected);
    bool isCustomBreakSelected() const noexcept { return customBreakSelected.load(); }

    BreakData::Ptr getBreakData() const;
    BreakData::Ptr getCustomBreakData() const;

    double getDetectedBpm() const noexcept { return detectedBpm.load(); }
    bool   hasSample() const;
    float  getPlayheadNormalised() const noexcept { return engine.getPlayheadNormalised(); }

    void triggerPreviewOn (int midiNote);
    void triggerPreviewOff (int midiNote);
    void setPitchWheelFromUI (float semitones) noexcept
    {
        pitchWheelSemitones.store (juce::jlimit (-2.0f, 2.0f, semitones));
    }
    float getPitchWheelSemitones() const noexcept { return pitchWheelSemitones.load(); }
    void setModWheelFromUI (float amount) noexcept
    {
        modWheelAmount.store (juce::jlimit (0.0f, 1.0f, amount));
        toneStackDirty.store (true);
    }
    float getModWheelAmount() const noexcept { return modWheelAmount.load(); }
    float getLiveFilterCutoff() const noexcept { return toneStack.getLiveFilterCutoff(); }
    float getLiveFilterResonance() const noexcept { return toneStack.getLiveFilterResonance(); }
    OfflineExporter::Snapshot captureExportSnapshot() const;
    licensing::LicenseClient& getLicenseClient() noexcept { return licenseClient; }
    bool isLicensed() const noexcept { return licenseClient.isVerified(); }
    /** The logo intro plays once per plugin instance; the flag is saved with the host project. */
    bool hasPlayedLogoIntro() const noexcept { return logoIntroPlayed.load(); }
    void markLogoIntroPlayed() noexcept { logoIntroPlayed.store (true); }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    SpectrumTap spectrumTap;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void updateToneStack();
    void clearLoadedSession();
    void publish (BreakData::Ptr newData);
    void captureEffectSettings (std::vector<float>& destination) const;
    void applyEffectSettings (const std::vector<float>& settings);
    std::vector<juce::RangedAudioParameter*> getEffectParameters();

    juce::AudioFormatManager formatManager;
    licensing::LicenseClient licenseClient;

    BreakEngine engine;
    ToneStack   toneStack;

    mutable juce::SpinLock arrangementLock;
    mutable juce::CriticalSection effectSettingsLock;
    BreakData::Ptr mainData, customData;
    std::atomic<bool> customBreakSelected { false };
    std::atomic<bool> sampleBroken { false };
    std::atomic<bool> logoIntroPlayed { false };
    std::vector<float> mainEffectSettings, customEffectSettings;

    juce::File   loadedFile;
    std::atomic<double> detectedBpm { 0.0 };
    std::atomic<bool>   toneStackDirty { true };
    std::atomic<bool>   resetRequested { false };
    std::atomic<bool>   restoringState { false };
    std::atomic<juce::uint64> presetRevision { 0 };
    std::atomic<float>  pitchWheelSemitones { 0.0f };
    std::atomic<float>  modWheelAmount { 0.0f };

    juce::SmoothedValue<float> outputGain { 1.0f };
    bool licensedLastBlock = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (DrumBreakerProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumBreakerProcessor)
};

} // namespace drumbreaker
