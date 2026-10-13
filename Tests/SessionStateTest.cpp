#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "UI/ActivationPanel.h"
#include "LicenseTestSupport.h"

#include <iostream>

using namespace drumbreaker;

static void setParameter (DrumBreakerProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.apvts.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialise;
    licensing::LicenseClient::Options licensingOptions;
    licensingOptions.storageScope = "session-test-" + juce::Uuid().toString();
    licensingOptions.restoreOnStartup = false;
    licensingOptions.transport = [] (const licensing::LicenseRequest&)
    {
        return licensing::LicenseResponse { juce::Result::fail ("No network in session tests."), 0, {} };
    };
    int failures = 0;
    auto check = [&failures] (bool passed, const char* description)
    {
        if (! passed)
        {
            std::cerr << "FAILED: " << description << "\n";
            ++failures;
        }
    };
    auto defaults = [&check] (DrumBreakerProcessor& processor)
    {
        for (auto* parameter : processor.getParameters())
            check (std::abs (parameter->getValue() - parameter->getDefaultValue()) < 0.00001f,
                   "fresh instance has factory parameter values");
        check (! processor.hasSample() && processor.getBreakData() == nullptr
                && processor.getDetectedBpm() == 0.0, "fresh instance has no sample or BPM");
    };
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("DrumBreaker-session-test", "", false);
    if (directory.createDirectory().failed())
        return 1;
    struct Cleanup
    {
        juce::File directory;
        ~Cleanup() { directory.deleteRecursively(); }
    } cleanup { directory };
    const auto sourceFile = directory.getChildFile ("session.wav");
    {
        auto stream = sourceFile.createOutputStream();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (stream.get(), 44100.0, 1, 16, {}, 0));
        if (writer == nullptr)
            return 1;
        stream.release();
        juce::AudioBuffer<float> audio (1, 88200);
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample (0, sample, (float) (0.25 * std::sin (
                2.0 * juce::MathConstants<double>::pi * 220.0 * sample / 44100.0)));
        if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            return 1;
    }

    DrumBreakerProcessor fresh (licensingOptions);
    defaults (fresh);
    juce::MemoryBlock emptyState;
    fresh.getStateInformation (emptyState);

    DrumBreakerProcessor projectA (licensingOptions);
    juce::String error;
    check (projectA.loadSampleFile (sourceFile, error), "project A loads its sample");
    projectA.breakSample();
    projectA.setArrangement ({ 0, 0 });
    setParameter (projectA, params::filterPreset, 1.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    setParameter (projectA, params::eqMidGain, 6.0f);
    setParameter (projectA, params::filterCutoff, 1234.0f);
    setParameter (projectA, params::reverb, 0.3f);
    setParameter (projectA, params::pitch, 3.0f);
    projectA.setPitchWheelFromUI (0.75f);
    projectA.setModWheelFromUI (0.45f);
    juce::MemoryBlock savedA;
    projectA.getStateInformation (savedA);

    DrumBreakerProcessor projectB (licensingOptions);
    defaults (projectB);
    check (projectA.hasSample(), "creating project B leaves project A untouched");

    DrumBreakerProcessor restored (licensingOptions);
    restored.setStateInformation (savedA.getData(), (int) savedA.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    check (restored.hasSample() && restored.getBreakData()->name == "session.wav", "saved project restores its sample");
    check (restored.getBreakData() != nullptr
            && restored.getBreakData()->arrangement == std::vector<int> { 0, 0 }, "saved project restores its arrangement");
        check (std::abs (restored.getPitchWheelSemitones() - 0.75f) < 0.00001f
            && std::abs (restored.getModWheelAmount() - 0.45f) < 0.00001f,
           "saved project restores its pitch/modulation wheel preferences");
    for (auto* parameter : projectA.getParameters())
    {
        auto* sourceParameter = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        check (sourceParameter != nullptr, "parameter is addressable by ID");
        if (sourceParameter != nullptr)
            check (std::abs (sourceParameter->getValue()
                    - restored.apvts.getParameter (sourceParameter->paramID)->getValue()) < 0.00001f,
                   "saved project restores custom settings without reapplying its genre preset");
    }

    setParameter (restored, params::filterPreset, 2.0f);
    restored.setStateInformation (savedA.getData(), (int) savedA.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    check (std::abs (restored.apvts.getRawParameterValue (params::eqMidGain)->load() - 6.0f) < 0.01f,
           "queued preset from previous state cannot overwrite restored preferences");

    {
        licenseTest::FixtureStore licenseStore;
        auto gateOptions = licenseStore.options();
        std::atomic<bool> reject { false };
        std::atomic<bool> holdValidation { false };
        juce::WaitableEvent validationStarted, releaseValidation;
        gateOptions.transport = [&] (const licensing::LicenseRequest& request)
        {
            if (holdValidation.load())
            {
                validationStarted.signal();
                if (! releaseValidation.wait (5000))
                    return licensing::LicenseResponse { juce::Result::fail ("fixture validation timeout"), 0, {} };
            }
            if (reject.load())
                return licensing::LicenseResponse { juce::Result::ok(), 200,
                    R"({"valid":false,"activated":false,"error":"fixture revoked"})" };
            return licenseTest::approvedReply (request);
        };
        DrumBreakerProcessor gated (gateOptions);
        gated.setStateInformation (savedA.getData(), (int) savedA.getSize());
        gated.prepareToPlay (44100.0, 512);
        juce::AudioBuffer<float> output (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        gated.triggerPreviewOn (60);
        gated.processBlock (output, midi);
        check (! gated.isLicensed() && output.getMagnitude (0, 512) == 0.0f,
               "loaded project plus host/preview MIDI cannot play before license verification");
        const auto deniedFile = directory.getChildFile ("processor-unlicensed.wav");
        auto denied = gated.captureExportSnapshot();
        check (denied.data == nullptr && OfflineExporter::writeWav (denied, deniedFile).failed()
                && ! deniedFile.exists(), "processor export snapshots are denied before verification");
        check (gated.getLicenseClient().activate (licenseTest::key)
                && licenseTest::waitForIdle (gated.getLicenseClient()) && gated.isLicensed(),
               "trusted successful verification unlocks the processor");
        midi.clear();
        gated.processBlock (output, midi);
        check (output.getMagnitude (0, 512) == 0.0f, "locked notes are discarded rather than replayed after activation");
        float peak = 0.0f;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        for (int block = 0; block < 12; ++block)
        {
            gated.processBlock (output, midi);
            peak = juce::jmax (peak, output.getMagnitude (0, 512));
            midi.clear();
        }
        check (peak > 0.0001f, "verified instrument renders audible playback");
        const auto allowedFile = directory.getChildFile ("processor-licensed.wav");
        const auto allowed = gated.captureExportSnapshot();
        check (allowed.data != nullptr && OfflineExporter::writeWav (allowed, allowedFile).wasOk(),
               "verified processor can render its full processed arrangement");
        juce::MemoryBlock licensedState;
        gated.getStateInformation (licensedState);
        DrumBreakerProcessor stateOnly (licensingOptions);
        stateOnly.setStateInformation (licensedState.getData(), (int) licensedState.getSize());
        check (! stateOnly.isLicensed(), "DAW project state cannot transfer or forge license authorization");
         holdValidation.store (true);
         check (gated.getLicenseClient().validateSaved() && validationStarted.wait (3000),
             "saved validation can be observed while network response is pending");
         midi.clear();
         midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
         gated.processBlock (output, midi);
         check (! gated.isLicensed() && output.getMagnitude (0, 512) == 0.0f,
             "pending validation mutes an already playing instrument");
         const auto pendingFile = directory.getChildFile ("pending-license.wav");
         check (OfflineExporter::writeWav (allowed, pendingFile).failed() && ! pendingFile.exists(),
             "pending verification denies previously captured export access");
         releaseValidation.signal();
         check (licenseTest::waitForIdle (gated.getLicenseClient()) && gated.isLicensed(),
             "successful completed revalidation restores authorization");
         holdValidation.store (false);
        reject.store (true);
        check (gated.getLicenseClient().validateSaved()
                && licenseTest::waitForIdle (gated.getLicenseClient()) && ! gated.isLicensed(),
               "rejected saved license revokes processor authorization");
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        for (int block = 0; block < 6; ++block)
        {
            gated.processBlock (output, midi);
            check (output.getMagnitude (0, 512) == 0.0f, "lost authorization mutes notes and effect tails");
            midi.clear();
        }
        const auto staleFile = directory.getChildFile ("stale-snapshot.wav");
        check (OfflineExporter::writeWav (allowed, staleFile).failed() && ! staleFile.exists(),
               "previously authorized export snapshot is denied after license rejection");
    }

    restored.setPitchWheelFromUI (2.0f);
    restored.setModWheelFromUI (1.0f);
    restored.setStateInformation (emptyState.getData(), (int) emptyState.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    defaults (restored);
    check (restored.getPitchWheelSemitones() == 0.0f && restored.getModWheelAmount() == 0.0f,
           "restoring empty project clears previous controller state");

    projectA.setArrangement ({});
    juce::MemoryBlock deletedArrangement;
    projectA.getStateInformation (deletedArrangement);
    restored.setStateInformation (deletedArrangement.getData(), (int) deletedArrangement.getSize());
    check (restored.hasSample() && restored.getBreakData()->arrangement.empty()
            && restored.getBreakData()->totalLength == 0, "saved fully deleted arrangement does not reload the whole sample");

    check (sourceFile.deleteFile(), "fixture can be removed to simulate a missing sample");
    restored.setStateInformation (savedA.getData(), (int) savedA.getSize());
    check (! restored.hasSample() && restored.getBreakData() == nullptr && restored.getDetectedBpm() == 0.0,
           "missing saved sample cannot retain a previous project's sample");
    check (std::abs (restored.apvts.getRawParameterValue (params::reverb)->load() - 0.3f) < 0.01f,
           "missing sample still restores the project's parameter preferences");

    {
        DrumBreakerProcessor temporary (licensingOptions);
        setParameter (temporary, params::filterPreset, 1.0f);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    auto uiLicenseOptions = licensingOptions;
    uiLicenseOptions.storageScope = "editor-reopen-test-" + juce::Uuid().toString();
    licensing::LicenseStore uiStore (uiLicenseOptions.storageScope);
    struct CredentialCleanup
    {
        licensing::LicenseStore& store;
        ~CredentialCleanup() { store.remove(); }
    } credentialCleanup { uiStore };
    uiLicenseOptions.transport = [] (const licensing::LicenseRequest& request)
    {
        auto reply = juce::JSON::parse (R"({
            "activated":true,"valid":true,"error":null,
            "license_key":{"status":"active","key":"fixture-only-not-a-customer-key","expires_at":null},
            "instance":{"id":"fixture-instance","name":"fixture-device"},
            "meta":{"product_id":1412490,"variant_id":2205337}
        })");
        if (request.kind == licensing::ReplyKind::preflight)
            reply.getDynamicObject()->setProperty ("instance", juce::var());
        return licensing::LicenseResponse { juce::Result::ok(), 200, juce::JSON::toString (reply) };
    };
    licensing::LicenseClient verifiedClient ("fixture-device", uiLicenseOptions);
    check (verifiedClient.activate ("fixture-only-not-a-customer-key"), "mock activation starts for editor reopen test");
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
    juce::WaitableEvent pause;
    while (verifiedClient.getStatus().busy && juce::Time::getMillisecondCounterHiRes() < deadline)
        pause.wait (5);
    check (verifiedClient.getStatus().verified, "editor reopen fixture is verified");
    for (int opening = 0; opening < 2; ++opening)
    {
        ActivationPanel panel (verifiedClient);
        panel.setBounds (0, 0, 900, 600);
        panel.showIfUnverified();
        check (! panel.isVisible(), "recreated editor panel never prompts an already verified client");
    }
    {
        ActivationPanel panel (verifiedClient);
        panel.setBounds (0, 0, 900, 600);
        panel.showActivation();
        check (panel.isVisible(), "manual activation/status access remains available");
        bool activateFound = false;
        for (int child = 0; child < panel.getNumChildComponents(); ++child)
            if (auto* button = dynamic_cast<juce::TextButton*> (panel.getChildComponent (child)))
                if (button->getButtonText() == "ACTIVATE")
                {
                    activateFound = true;
                    check (! button->isVisible(), "verified license form hides the activation button");
                }
        check (activateFound, "verified form activation action is present but hidden");
    }
    {
        licensing::LicenseClient unverifiedClient ("fixture-device", licensingOptions);
        ActivationPanel panel (unverifiedClient);
        panel.setBounds (0, 0, 900, 600);
        panel.showIfUnverified();
        check (panel.isVisible(), "unverified client still gets an automatic activation prompt");
        bool activateVisible = false;
        for (int child = 0; child < panel.getNumChildComponents(); ++child)
            if (auto* button = dynamic_cast<juce::TextButton*> (panel.getChildComponent (child)))
                activateVisible = activateVisible || (button->getButtonText() == "ACTIVATE" && button->isVisible());
        check (activateVisible, "unverified form retains the activation action");
    }
    {
        ActivationStatusButton indicator ("ACTIVATE");
        indicator.setBounds (0, 0, 100, 22);
        indicator.setLicenseVerified (true);
        check (indicator.getButtonText() == "ACTIVATED", "verified header displays ACTIVATED");
        juce::Image preview (juce::Image::ARGB, 100, 22, true);
        {
            juce::Graphics graphics (preview);
            indicator.paintButton (graphics, false, false);
        }
        bool tickDrawn = false;
        for (int positionX = 80; positionX < 94; ++positionX)
            for (int positionY = 6; positionY < 17; ++positionY)
                tickDrawn = tickDrawn || preview.getPixelAt (positionX, positionY).getBrightness() > 0.6f;
        check (tickDrawn, "verified header renders a visible checkmark");
        indicator.setLicenseVerified (false);
        check (indicator.getButtonText() == "ACTIVATE", "lost verification restores ACTIVATE label");
    }
    std::cout << "Session state checks: " << (failures == 0 ? "PASS" : "FAIL") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}