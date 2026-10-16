#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "Audio/FilterPresets.h"

#include <numeric>

namespace drumbreaker
{

using APVTS = juce::AudioProcessorValueTreeState;

static std::vector<const char*> getEffectParameterIDs()
{
    std::vector<const char*> ids { params::eqEnabled };

    for (size_t band = 0; band < params::eqGainIDs.size(); ++band)
    {
        ids.push_back (params::eqGainIDs[band]);
        ids.push_back (params::eqFrequencyIDs[band]);
        if (params::eqQIDs[band] != nullptr)
            ids.push_back (params::eqQIDs[band]);
        ids.push_back (params::eqTypeIDs[band]);
    }

    for (const auto* id : { params::filterType, params::filterCutoff, params::filterReso,
                            params::filterDrive, params::filterMix, params::filterPreset,
                            params::filterEnabled, params::filterMotion })
        ids.push_back (id);

    return ids;
}

static juce::String encodeNumbers (const std::vector<float>& values)
{
    juce::StringArray tokens;
    for (float value : values)
        tokens.add (juce::String (value, 8));
    return tokens.joinIntoString (",");
}

static std::vector<float> decodeNumbers (const juce::String& text)
{
    std::vector<float> values;
    if (text.isEmpty())
        return values;

    for (const auto& token : juce::StringArray::fromTokens (text, ",", ""))
    {
        const double value = token.getDoubleValue();
        if (! std::isfinite (value))
            return {};
        values.push_back ((float) value);
    }
    return values;
}

static juce::String encodeSlices (const std::vector<Slice>& slices)
{
    juce::StringArray tokens;
    for (const auto& slice : slices)
        tokens.add (juce::String (slice.start) + ":" + juce::String (slice.end));
    return tokens.joinIntoString (",");
}

static std::vector<Slice> decodeSlices (const juce::String& text)
{
    std::vector<Slice> slices;
    if (text.isEmpty())
        return slices;

    for (const auto& token : juce::StringArray::fromTokens (text, ",", ""))
    {
        const auto parts = juce::StringArray::fromTokens (token, ":", "");
        if (parts.size() != 2)
            return {};
        slices.push_back ({ parts[0].getLargeIntValue(), parts[1].getLargeIntValue() });
    }
    return slices;
}

static juce::String encodeOrder (const std::vector<int>& order)
{
    juce::StringArray tokens;
    for (int index : order)
        tokens.add (juce::String (index));
    return tokens.joinIntoString (",");
}

static std::vector<int> decodeOrder (const juce::String& text)
{
    std::vector<int> order;
    if (text.isEmpty())
        return order;

    for (const auto& token : juce::StringArray::fromTokens (text, ",", ""))
        order.push_back (token.getIntValue());
    return order;
}

static juce::String hzToText (float value, int)
{
    return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " kHz"
                            : juce::String ((int) value) + " Hz";
}

APVTS::ParameterLayout DrumBreakerProcessor::createParameterLayout()
{
    APVTS::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { params::pitch, 1 }, "Pitch",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { params::outputGain, 1 }, "Output",
        juce::NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { params::reverb, 1 }, "Reverb",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { params::sensitivity, 1 }, "Sensitivity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.55f));

    for (size_t band = 0; band < params::eqGainIDs.size(); ++band)
    {
        const auto name = "EQ " + juce::String ((int) band + 1);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::eqGainIDs[band], 1 }, name + " Gain",
            juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        juce::NormalisableRange<float> frequencyRange (params::eqMinFrequency, params::eqMaxFrequency, 0.01f);
        frequencyRange.setSkewForCentre (params::eqFrequencyAtPosition (0.5f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::eqFrequencyIDs[band], 1 }, name + " Frequency",
            frequencyRange, params::eqDefaultFrequency (band),
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzToText)));

        if (params::eqQIDs[band] != nullptr)
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { params::eqQIDs[band], 1 }, name + " Width",
                juce::NormalisableRange<float> (0.2f, 6.0f, 0.01f, 0.5f), 0.9f));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::eqTypeIDs[band], 1 }, name + " Type",
            juce::StringArray { "Default", "Bell", "Low Shelf", "High Shelf", "Low Pass", "High Pass" }, 0));
    }

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { params::eqEnabled, 1 }, "EQ Enabled", true));

    // Filter
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { params::filterType, 1 }, "Filter Type",
        juce::StringArray { "Low Pass", "Band Pass", "High Pass", "Notch" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { params::filterCutoff, 1 }, "Cutoff",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 0.01f, 0.25f), ToneStack::filterPadCentreCutoff,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { params::filterReso, 1 }, "Resonance",
        juce::NormalisableRange<float> (0.1f, 8.0f, 0.01f, 0.6f), ToneStack::filterPadCentreResonance));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { params::filterDrive, 1 }, "Drive",
        juce::NormalisableRange<float> (1.0f, 6.0f, 0.01f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { params::filterMix, 1 }, "Filter Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { params::filterPreset, 1 }, "Filter Preset",
        getFilterPresetNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { params::filterEnabled, 1 }, "Filter Enabled", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { params::filterMotion, 1 }, "Filter Motion",
        juce::StringArray { "None", "Low Pass Filter Sweep", "High Pass Filter Sweep", "Resonance Modulation",
                            "Band Pass Filter Sweep" }, 0));

    return layout;
}

//==============================================================================
DrumBreakerProcessor::DrumBreakerProcessor (licensing::LicenseClient::Options licensingOptions)
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
    apvts (*this, nullptr, "DRUMBREAKER", createParameterLayout()),
    licenseClient (licensing::makeDeviceName(), std::move (licensingOptions))
{
    formatManager.registerBasicFormats();

    for (const char* id : { params::filterType,
                            params::filterCutoff, params::filterReso, params::filterDrive, params::filterMix,
                            params::reverb, params::eqEnabled, params::filterEnabled, params::filterMotion })
        apvts.addParameterListener (id, this);

    for (size_t band = 0; band < params::eqGainIDs.size(); ++band)
        for (const char* id : { params::eqGainIDs[band], params::eqFrequencyIDs[band],
                    params::eqQIDs[band], params::eqTypeIDs[band] })
            if (id != nullptr)
                apvts.addParameterListener (id, this);

    apvts.addParameterListener (params::filterPreset, this);

    captureEffectSettings (mainEffectSettings);
}

DrumBreakerProcessor::~DrumBreakerProcessor()
{
    for (const char* id : { params::filterType,
                            params::filterCutoff, params::filterReso, params::filterDrive, params::filterMix,
                            params::filterPreset, params::reverb, params::eqEnabled,
                            params::filterEnabled, params::filterMotion })
        apvts.removeParameterListener (id, this);

    for (size_t band = 0; band < params::eqGainIDs.size(); ++band)
        for (const char* id : { params::eqGainIDs[band], params::eqFrequencyIDs[band],
                    params::eqQIDs[band], params::eqTypeIDs[band] })
            if (id != nullptr)
                apvts.removeParameterListener (id, this);
}

void DrumBreakerProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID == params::filterPreset)
    {
        const auto revision = presetRevision.fetch_add (1) + 1;
        if (restoringState.load())
        {
            toneStackDirty.store (true);
            return;
        }
        const int index = (int) newValue;

        if (index > 0)
            juce::MessageManager::callAsync ([weakThis = juce::WeakReference<DrumBreakerProcessor> (this), index, revision]
            {
                if (weakThis != nullptr && weakThis->presetRevision.load() == revision
                    && (int) weakThis->apvts.getRawParameterValue (params::filterPreset)->load() == index)
                    applyFilterPreset (weakThis->apvts, index);
            });

        return;
    }

    toneStackDirty.store (true);
}

void DrumBreakerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    licensedLastBlock = false;
    spectrumTap.setSampleRate (sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());

    toneStack.prepare (spec);
    toneStackDirty.store (true);

    outputGain.reset (sampleRate, 0.02);
    keyboardState.reset();
}

void DrumBreakerProcessor::releaseResources()
{
    engine.reset();
    toneStack.reset();
}

bool DrumBreakerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void DrumBreakerProcessor::updateToneStack()
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    ToneStack::EQSettings eqSettings;
    for (size_t band = 0; band < eqSettings.size(); ++band)
        eqSettings[band] = { get (params::eqGainIDs[band]), get (params::eqFrequencyIDs[band]),
                            params::eqQIDs[band] != nullptr ? get (params::eqQIDs[band]) : 0.707f,
                            (ToneStack::EQType) (int) get (params::eqTypeIDs[band]) };
    toneStack.setEQ (eqSettings);
    toneStack.setEQEnabled (get (params::eqEnabled) >= 0.5f);

    toneStack.setFilter ((ToneStack::FilterType) (int) get (params::filterType),
                         get (params::filterCutoff),
                         get (params::filterReso),
                         get (params::filterDrive),
                         get (params::filterMix),
                         modWheelAmount.load());
    toneStack.setReverb (get (params::reverb));
    toneStack.setFilterEnabled (get (params::filterEnabled) >= 0.5f);
    toneStack.setFilterMotion ((ToneStack::FilterMotion) (int) get (params::filterMotion));
}

void DrumBreakerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    if (! isLicensed())
    {
        if (licensedLastBlock || resetRequested.exchange (false))
        {
            engine.reset();
            toneStack.reset();
            outputGain.setCurrentAndTargetValue (1.0f);
            toneStackDirty.store (true);
        }
        licensedLastBlock = false;
        keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);
        keyboardState.reset();
        midiMessages.clear();
        spectrumTap.pushBuffer (buffer);
        return;
    }
    licensedLastBlock = true;

    keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);

    if (resetRequested.exchange (false))
    {
        engine.reset();
        toneStack.reset();
        outputGain.setCurrentAndTargetValue (1.0f);
        midiMessages.clear();
    }

    bool restartFilter = false;
    for (const auto meta : midiMessages)
    {
        const auto message = meta.getMessage();

        if (message.isNoteOn())
        {
            engine.noteOn (message.getNoteNumber(), message.getFloatVelocity());
            restartFilter = true;
        }
        else if (message.isNoteOff())
            engine.noteOff (message.getNoteNumber());
        else if (message.isPitchWheel())
            pitchWheelSemitones.store ((message.getPitchWheelValue() - 8192) * (2.0f / 8192.0f));
        else if (message.isController() && message.getControllerNumber() == 1)
        {
            modWheelAmount.store ((float) message.getControllerValue() / 127.0f);
            toneStackDirty.store (true);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            engine.allNotesOff();
    }

    engine.setPitchOffsetSemitones (apvts.getRawParameterValue (params::pitch)->load()
                                    + pitchWheelSemitones.load());
    const bool playingForFilter = engine.isPlaying();
    engine.renderNextBlock (buffer, 0, buffer.getNumSamples());

    if (toneStackDirty.exchange (false))
        updateToneStack();

    const auto sampleData = engine.getData();
    const double duration = sampleData != nullptr && sampleData->sourceSampleRate > 0.0
        ? (double) sampleData->totalLength / sampleData->sourceSampleRate / engine.getPlaybackPitchRatio() : 0.0;
    toneStack.setFilterPlayback (duration, playingForFilter, restartFilter);
    toneStack.process (buffer);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (params::outputGain)->load()));
    outputGain.applyGain (buffer, buffer.getNumSamples());

    if (! isLicensed())
        buffer.clear();
    spectrumTap.pushBuffer (buffer);
}

juce::AudioProcessorEditor* DrumBreakerProcessor::createEditor()
{
    return new DrumBreakerEditor (*this);
}

OfflineExporter::Snapshot DrumBreakerProcessor::captureExportSnapshot() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    OfflineExporter::Snapshot snapshot;
    snapshot.license = licenseClient.getAccess();
    if (! isLicensed())
        return snapshot;
    snapshot.data = engine.getData();
    snapshot.sampleRate = getSampleRate() > 0.0 ? getSampleRate()
        : snapshot.data != nullptr ? snapshot.data->sourceSampleRate : 44100.0;
    snapshot.channels = juce::jlimit (1, 2, getTotalNumOutputChannels());
    for (size_t band = 0; band < snapshot.eq.size(); ++band)
        snapshot.eq[band] = { get (params::eqGainIDs[band]), get (params::eqFrequencyIDs[band]),
                              params::eqQIDs[band] != nullptr ? get (params::eqQIDs[band]) : 0.707f,
                              (ToneStack::EQType) (int) get (params::eqTypeIDs[band]) };
    snapshot.filter = { (ToneStack::FilterType) (int) get (params::filterType),
                         get (params::filterCutoff), get (params::filterReso) };
    snapshot.motion = (ToneStack::FilterMotion) (int) get (params::filterMotion);
    snapshot.pitchSemitones = get (params::pitch) + pitchWheelSemitones.load();
    snapshot.filterDrive = get (params::filterDrive);
    snapshot.filterMix = get (params::filterMix);
    snapshot.modulation = modWheelAmount.load();
    snapshot.reverb = get (params::reverb);
    snapshot.outputDb = get (params::outputGain);
    snapshot.eqEnabled = get (params::eqEnabled) >= 0.5f;
    snapshot.filterEnabled = get (params::filterEnabled) >= 0.5f;
    return snapshot;
}

//==============================================================================
bool DrumBreakerProcessor::hasSample() const
{
    auto d = getBreakData();
    return d != nullptr && d->audio != nullptr && d->audio->getNumSamples() > 0;
}

void DrumBreakerProcessor::publish (BreakData::Ptr newData)
{
    if (newData != nullptr)
        newData->rebuildTimeline();

    BreakData::Ptr activeData;
    bool clearCustomSettings = false;
    {
        const juce::SpinLock::ScopedLockType sl (arrangementLock);
        mainData = newData;
        if (customData != nullptr && (newData == nullptr || customData->audio != newData->audio))
        {
            customData = nullptr;
            customBreakSelected.store (false);
            clearCustomSettings = true;
        }
        activeData = customBreakSelected.load() ? customData : mainData;
    }
    if (clearCustomSettings)
    {
        const juce::ScopedLock sl (effectSettingsLock);
        customEffectSettings.clear();
    }
    engine.setData (activeData);
    sendChangeMessage();
}

BreakData::Ptr DrumBreakerProcessor::getBreakData() const
{
    const juce::SpinLock::ScopedLockType sl (arrangementLock);
    return mainData;
}

BreakData::Ptr DrumBreakerProcessor::getCustomBreakData() const
{
    const juce::SpinLock::ScopedLockType sl (arrangementLock);
    return customData;
}

bool DrumBreakerProcessor::hasCustomBreak() const
{
    return getCustomBreakData() != nullptr;
}

std::vector<juce::RangedAudioParameter*> DrumBreakerProcessor::getEffectParameters()
{
    std::vector<juce::RangedAudioParameter*> parameters;
    for (const auto* id : getEffectParameterIDs())
        if (auto* parameter = apvts.getParameter (id))
            parameters.push_back (parameter);
    return parameters;
}

void DrumBreakerProcessor::captureEffectSettings (std::vector<float>& destination) const
{
    destination.clear();
    for (const auto* id : getEffectParameterIDs())
        if (auto* parameter = apvts.getParameter (id))
            destination.push_back (parameter->getValue());
}

void DrumBreakerProcessor::applyEffectSettings (const std::vector<float>& settings)
{
    const auto parameters = getEffectParameters();
    if (settings.size() != parameters.size())
        return;

    restoringState.store (true);
    for (size_t i = 0; i < parameters.size(); ++i)
        parameters[i]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, settings[i]));
    restoringState.store (false);
    toneStackDirty.store (true);
}

void DrumBreakerProcessor::selectCustomBreak (bool selected)
{
    auto custom = getCustomBreakData();
    if (selected && custom == nullptr)
        return;

    if (customBreakSelected.load() == selected)
        return;

    std::vector<float> currentSettings;
    captureEffectSettings (currentSettings);
    std::vector<float> nextSettings;
    {
        const juce::ScopedLock sl (effectSettingsLock);
        auto& activeSettings = customBreakSelected.load() ? customEffectSettings : mainEffectSettings;
        activeSettings = std::move (currentSettings);

        auto& nextBank = selected ? customEffectSettings : mainEffectSettings;
        if (nextBank.empty())
            nextBank = activeSettings;
        nextSettings = nextBank;
        customBreakSelected.store (selected);
    }

    engine.setData (selected ? custom : getBreakData(), true);
    applyEffectSettings (nextSettings);
}

bool DrumBreakerProcessor::createCustomBreak()
{
    auto source = getBreakData();
    if (source == nullptr || source->audio == nullptr || ! sampleBroken.load() || hasCustomBreak())
        return false;

    auto custom = new BreakData();
    custom->audio = source->audio;
    custom->sourceSampleRate = source->sourceSampleRate;
    custom->name = source->name;
    custom->slices = source->slices;
    custom->rebuildTimeline();

    std::vector<float> activeSettings;
    captureEffectSettings (activeSettings);
    {
        const juce::ScopedLock sl (effectSettingsLock);
        auto& activeBank = customBreakSelected.load() ? customEffectSettings : mainEffectSettings;
        activeBank = activeSettings;
        customEffectSettings = std::move (activeSettings);
    }

    {
        const juce::SpinLock::ScopedLockType sl (arrangementLock);
        customData = custom;
    }
    selectCustomBreak (true);
    sendChangeMessage();
    return true;
}

void DrumBreakerProcessor::resetToDefaults()
{
    for (auto* parameter : getParameters())
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getDefaultValue());
        parameter->endChangeGesture();
    }

    clearLoadedSession();
}

void DrumBreakerProcessor::clearLoadedSession()
{
    presetRevision.fetch_add (1);
    keyboardState.reset();
    pitchWheelSemitones.store (0.0f);
    modWheelAmount.store (0.0f);
    loadedFile = juce::File();
    detectedBpm.store (0.0);
    customBreakSelected.store (false);
    sampleBroken.store (false);
    {
        const juce::ScopedLock sl (effectSettingsLock);
        mainEffectSettings.clear();
        customEffectSettings.clear();
    }
    toneStackDirty.store (true);
    resetRequested.store (true);
    publish (nullptr);
}

bool DrumBreakerProcessor::loadSampleFile (const juce::File& file, juce::String& errorMessage)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        errorMessage = "Unsupported audio file: " + file.getFileName();
        return false;
    }

    const double seconds = (double) reader->lengthInSamples / juce::jmax (1.0, reader->sampleRate);

    if (seconds > kMaxSampleSeconds)
    {
        errorMessage = "Sample is " + juce::String (seconds, 1) + " s - the maximum length is 60 s";
        return false;
    }

    if (reader->lengthInSamples <= 0)
    {
        errorMessage = "That file contains no audio";
        return false;
    }

    auto audio = std::make_shared<juce::AudioBuffer<float>> ((int) juce::jlimit<juce::int64> (1, 2, reader->numChannels),
                                                             (int) reader->lengthInSamples);
    audio->clear();
    reader->read (audio.get(), 0, (int) reader->lengthInSamples, 0, true, true);

    if (audio->getMagnitude (0, audio->getNumSamples()) < 1.0e-6f)
    {
        errorMessage = file.getFileName() + " contains only silence";
        return false;
    }

    const double bpm = SampleAnalyzer::detectTempo (*audio, reader->sampleRate);
    detectedBpm.store (bpm);

    BreakData::Ptr newData = new BreakData();
    newData->audio = audio;
    newData->sourceSampleRate = reader->sampleRate;
    newData->name = file.getFileName();
    newData->slices.push_back ({ 0, (juce::int64) audio->getNumSamples() });
    newData->arrangement = { 0 };

    loadedFile = file;
    customBreakSelected.store (false);
    sampleBroken.store (false);
    {
        const juce::SpinLock::ScopedLockType sl (arrangementLock);
        customData = nullptr;
    }
    std::vector<float> initialSettings;
    captureEffectSettings (initialSettings);
    {
        const juce::ScopedLock sl (effectSettingsLock);
        customEffectSettings.clear();
        mainEffectSettings = std::move (initialSettings);
    }
    publish (newData);

    return true;
}

void DrumBreakerProcessor::breakSample()
{
    auto current = getBreakData();

    if (current == nullptr || current->audio == nullptr)
        return;

    SampleAnalyzer::Settings settings;
    settings.sensitivity = apvts.getRawParameterValue (params::sensitivity)->load();

    BreakData::Ptr newData = new BreakData();
    newData->audio = current->audio;
    newData->sourceSampleRate = current->sourceSampleRate;
    newData->name = current->name;
    newData->slices = SampleAnalyzer::detectSlices (*current->audio, current->sourceSampleRate, settings);

    newData->arrangement.resize (newData->slices.size());
    std::iota (newData->arrangement.begin(), newData->arrangement.end(), 0);

    sampleBroken.store (true);
    publish (newData);
}

void DrumBreakerProcessor::restoreWholeSample()
{
    auto current = getBreakData();

    if (current == nullptr || current->audio == nullptr)
        return;

    BreakData::Ptr newData = new BreakData();
    newData->audio = current->audio;
    newData->sourceSampleRate = current->sourceSampleRate;
    newData->name = current->name;
    newData->slices.push_back ({ 0, (juce::int64) current->audio->getNumSamples() });
    newData->arrangement = { 0 };

    sampleBroken.store (false);
    publish (newData);
}

void DrumBreakerProcessor::setArrangement (const std::vector<int>& newArrangement)
{
    auto current = getBreakData();

    if (current == nullptr)
        return;

    auto updated = current->withArrangement (newArrangement);
    {
        const juce::SpinLock::ScopedLockType sl (arrangementLock);
        mainData = updated;
    }
    if (! customBreakSelected.load())
        engine.setData (updated);
}

void DrumBreakerProcessor::setCustomArrangement (const std::vector<int>& newArrangement)
{
    auto current = getCustomBreakData();
    if (current == nullptr)
        return;

    auto updated = current->withArrangement (newArrangement);
    {
        const juce::SpinLock::ScopedLockType sl (arrangementLock);
        customData = updated;
    }
    if (customBreakSelected.load())
        engine.setData (updated);
}

void DrumBreakerProcessor::triggerPreviewOn (int midiNote)
{
    if (! isLicensed())
        return;
    keyboardState.noteOn (1, midiNote, 1.0f);
}

void DrumBreakerProcessor::triggerPreviewOff (int midiNote)
{
    keyboardState.noteOff (1, midiNote, 0.0f);
}

//==============================================================================
void DrumBreakerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::vector<float> currentSettings;
    captureEffectSettings (currentSettings);
    std::vector<float> savedMainSettings, savedCustomSettings;
    {
        const juce::ScopedLock sl (effectSettingsLock);
        (customBreakSelected.load() ? customEffectSettings : mainEffectSettings) = std::move (currentSettings);
        savedMainSettings = mainEffectSettings;
        savedCustomSettings = customEffectSettings;
    }

    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml == nullptr)
        return;

    auto* performance = xml->createNewChildElement ("PERFORMANCE");
    performance->setAttribute ("pitchWheel", pitchWheelSemitones.load());
    performance->setAttribute ("modWheel", modWheelAmount.load());

    xml->createNewChildElement ("INTRO")->setAttribute ("played", logoIntroPlayed.load());
    auto d = getBreakData();
    if (d != nullptr)
    {
        auto* sample = xml->createNewChildElement ("SAMPLE");
        sample->setAttribute ("file", loadedFile.getFullPathName());
        sample->setAttribute ("bpm", detectedBpm.load());
        sample->setAttribute ("broken", sampleBroken.load());

        sample->setAttribute ("slices", encodeSlices (d->slices));
        sample->setAttribute ("order", encodeOrder (d->arrangement));
    }

    auto* effectBanks = xml->createNewChildElement ("EFFECT_BANKS");
    effectBanks->setAttribute ("filterMotionChoices", 5);
    effectBanks->setAttribute ("main", encodeNumbers (savedMainSettings));
    effectBanks->setAttribute ("custom", encodeNumbers (savedCustomSettings));

    if (auto custom = getCustomBreakData())
    {
        auto* customElement = xml->createNewChildElement ("CUSTOM_BREAK");
        customElement->setAttribute ("selected", customBreakSelected.load());
        customElement->setAttribute ("slices", encodeSlices (custom->slices));
        customElement->setAttribute ("order", encodeOrder (custom->arrangement));
    }

    copyXmlToBinary (*xml, destData);
}

void DrumBreakerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto* sample = xml->getChildByName ("SAMPLE");
    std::unique_ptr<juce::XmlElement> sampleCopy (sample != nullptr ? new juce::XmlElement (*sample) : nullptr);

    if (sample != nullptr)
        xml->removeChildElement (sample, true);

    auto* custom = xml->getChildByName ("CUSTOM_BREAK");
    std::unique_ptr<juce::XmlElement> customCopy (custom != nullptr ? new juce::XmlElement (*custom) : nullptr);
    if (custom != nullptr)
        xml->removeChildElement (custom, true);

    auto* effectBanks = xml->getChildByName ("EFFECT_BANKS");
    auto savedMainSettings = effectBanks != nullptr ? decodeNumbers (effectBanks->getStringAttribute ("main"))
                                                         : std::vector<float>();
    auto savedCustomSettings = effectBanks != nullptr ? decodeNumbers (effectBanks->getStringAttribute ("custom"))
                                                           : std::vector<float>();
    // Banks store normalized values; preserve old motion indices when adding Band Pass.
    if (effectBanks != nullptr && effectBanks->getIntAttribute ("filterMotionChoices", 4) == 4)
        for (auto* settings : { &savedMainSettings, &savedCustomSettings })
            if (settings->size() == getEffectParameterIDs().size())
                settings->back() *= 0.75f;
    if (effectBanks != nullptr)
        xml->removeChildElement (effectBanks, true);

    auto* performance = xml->getChildByName ("PERFORMANCE");
    const float restoredPitchWheel = performance != nullptr ? (float) performance->getDoubleAttribute ("pitchWheel", 0.0) : 0.0f;
    const float restoredModWheel = performance != nullptr ? (float) performance->getDoubleAttribute ("modWheel", 0.0) : 0.0f;
    if (performance != nullptr)
        xml->removeChildElement (performance, true);

    if (auto* intro = xml->getChildByName ("INTRO"))
    {
        if (intro->getBoolAttribute ("played", false))
            logoIntroPlayed.store (true);
        xml->removeChildElement (intro, true);
    }

    clearLoadedSession();
    restoringState.store (true);
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    restoringState.store (false);
    setPitchWheelFromUI (std::isfinite (restoredPitchWheel) ? restoredPitchWheel : 0.0f);
    setModWheelFromUI (std::isfinite (restoredModWheel) ? restoredModWheel : 0.0f);
    toneStackDirty.store (true);

    if (sampleCopy == nullptr)
        return;

    const juce::File file (sampleCopy->getStringAttribute ("file"));

    if (! file.existsAsFile())
        return;

    juce::String error;

    if (! loadSampleFile (file, error))
        return;

    auto current = engine.getData();

    if (current == nullptr)
        return;

    auto slices = decodeSlices (sampleCopy->getStringAttribute ("slices"));
    auto order = decodeOrder (sampleCopy->getStringAttribute ("order"));

    if (slices.empty())
        return;

    for (const auto& slice : slices)
        if (slice.start < 0 || slice.end > current->audio->getNumSamples() || slice.end <= slice.start)
        {
            clearLoadedSession();
            return;
        }
    for (int index : order)
        if (! juce::isPositiveAndBelow (index, (int) slices.size()))
        {
            clearLoadedSession();
            return;
        }

    BreakData::Ptr restored = new BreakData();
    restored->audio = current->audio;
    restored->sourceSampleRate = current->sourceSampleRate;
    restored->name = current->name;
    restored->slices = std::move (slices);
    restored->arrangement = std::move (order);

    sampleBroken.store (sampleCopy->getBoolAttribute ("broken", restored->slices.size() > 1));
    publish (restored);

    const auto expectedSettings = getEffectParameters().size();
    auto currentSettings = std::vector<float>();
    if (savedMainSettings.size() != expectedSettings)
        captureEffectSettings (currentSettings);
    {
        const juce::ScopedLock sl (effectSettingsLock);
        mainEffectSettings = savedMainSettings.size() == expectedSettings ? savedMainSettings
                                                                          : currentSettings;
        customEffectSettings = savedCustomSettings.size() == expectedSettings ? savedCustomSettings
                                                                              : std::vector<float>();
    }

    if (customCopy != nullptr)
    {
        auto customSlices = decodeSlices (customCopy->getStringAttribute ("slices"));
        auto customOrder = decodeOrder (customCopy->getStringAttribute ("order"));
        bool valid = ! customSlices.empty();
        for (const auto& slice : customSlices)
            valid = valid && slice.start >= 0 && slice.end <= current->audio->getNumSamples()
                    && slice.end > slice.start;
        for (int index : customOrder)
            valid = valid && juce::isPositiveAndBelow (index, (int) customSlices.size());

        if (valid)
        {
            auto customDataToRestore = new BreakData();
            customDataToRestore->audio = current->audio;
            customDataToRestore->sourceSampleRate = current->sourceSampleRate;
            customDataToRestore->name = current->name;
            customDataToRestore->slices = std::move (customSlices);
            customDataToRestore->arrangement = std::move (customOrder);
            customDataToRestore->rebuildTimeline();
            {
                const juce::SpinLock::ScopedLockType sl (arrangementLock);
                customData = customDataToRestore;
            }

            const bool selectCustom = customCopy->getBoolAttribute ("selected", false);
            customBreakSelected.store (selectCustom);
            engine.setData (selectCustom ? customDataToRestore : restored);
        }
    }

    toneStackDirty.store (true);
}

} // namespace drumbreaker

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new drumbreaker::DrumBreakerProcessor();
}
