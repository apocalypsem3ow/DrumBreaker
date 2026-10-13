#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "Audio/FilterPresets.h"

#include <numeric>

namespace drumbreaker
{

using APVTS = juce::AudioProcessorValueTreeState;

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
        juce::NormalisableRange<float> (20.0f, 20000.0f, 0.01f, 0.25f), 20000.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzToText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { params::filterReso, 1 }, "Resonance",
        juce::NormalisableRange<float> (0.1f, 8.0f, 0.01f, 0.6f), 0.707f));

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
        juce::StringArray { "None", "Low Pass Filter Sweep", "High Pass Filter Sweep", "Resonance Modulation" }, 0));

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
    auto d = engine.getData();
    return d != nullptr && d->audio != nullptr && d->audio->getNumSamples() > 0;
}

void DrumBreakerProcessor::publish (BreakData::Ptr newData)
{
    engine.setData (newData);
    sendChangeMessage();
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
    publish (newData);

    return true;
}

void DrumBreakerProcessor::breakSample()
{
    auto current = engine.getData();

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

    publish (newData);
}

void DrumBreakerProcessor::restoreWholeSample()
{
    auto current = engine.getData();

    if (current == nullptr || current->audio == nullptr)
        return;

    BreakData::Ptr newData = new BreakData();
    newData->audio = current->audio;
    newData->sourceSampleRate = current->sourceSampleRate;
    newData->name = current->name;
    newData->slices.push_back ({ 0, (juce::int64) current->audio->getNumSamples() });
    newData->arrangement = { 0 };

    publish (newData);
}

void DrumBreakerProcessor::setArrangement (const std::vector<int>& newArrangement)
{
    auto current = engine.getData();

    if (current == nullptr)
        return;

    engine.setData (current->withArrangement (newArrangement));
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
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml == nullptr)
        return;

    auto* performance = xml->createNewChildElement ("PERFORMANCE");
    performance->setAttribute ("pitchWheel", pitchWheelSemitones.load());
    performance->setAttribute ("modWheel", modWheelAmount.load());

    if (auto d = engine.getData())
    {
        auto* sample = xml->createNewChildElement ("SAMPLE");
        sample->setAttribute ("file", loadedFile.getFullPathName());
        sample->setAttribute ("bpm", detectedBpm.load());

        juce::StringArray sliceText, orderText;

        for (const auto& s : d->slices)
            sliceText.add (juce::String (s.start) + ":" + juce::String (s.end));

        for (int i : d->arrangement)
            orderText.add (juce::String (i));

        sample->setAttribute ("slices", sliceText.joinIntoString (","));
        sample->setAttribute ("order", orderText.joinIntoString (","));
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

    auto* performance = xml->getChildByName ("PERFORMANCE");
    const float restoredPitchWheel = performance != nullptr ? (float) performance->getDoubleAttribute ("pitchWheel", 0.0) : 0.0f;
    const float restoredModWheel = performance != nullptr ? (float) performance->getDoubleAttribute ("modWheel", 0.0) : 0.0f;
    if (performance != nullptr)
        xml->removeChildElement (performance, true);

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

    std::vector<Slice> slices;

    for (const auto& token : juce::StringArray::fromTokens (sampleCopy->getStringAttribute ("slices"), ",", ""))
    {
        const auto parts = juce::StringArray::fromTokens (token, ":", "");

        if (parts.size() == 2)
            slices.push_back ({ parts[0].getLargeIntValue(), parts[1].getLargeIntValue() });
    }

    std::vector<int> order;

    for (const auto& token : juce::StringArray::fromTokens (sampleCopy->getStringAttribute ("order"), ",", ""))
        order.push_back (token.getIntValue());

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

    publish (restored);
}

} // namespace drumbreaker

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new drumbreaker::DrumBreakerProcessor();
}
