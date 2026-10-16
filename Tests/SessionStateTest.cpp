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

static int runLogoIntroTest()
{
    int failures = 0;
    auto check = [&failures] (bool passed, const char* description)
    {
        if (! passed)
        {
            std::cerr << "FAILED: " << description << "\n";
            ++failures;
        }
    };
    auto waitUntil = [] (const std::function<bool()>& predicate, int milliseconds)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + milliseconds;
        while (! predicate() && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        return predicate();
    };
    licenseTest::FixtureStore store;
    auto options = store.options();
    std::atomic<bool> reject { false };
    options.transport = [&reject] (const licensing::LicenseRequest& request)
    {
        if (reject.load())
            return licensing::LicenseResponse { juce::Result::ok(), 200,
                R"({"valid":false,"activated":false,"error":"fixture revoked"})" };
        return licenseTest::approvedReply (request);
    };
    auto findIntro = [] (juce::Component& editor) -> LogoIntro*
    {
        for (int child = 0; child < editor.getNumChildComponents(); ++child)
            if (auto* candidate = dynamic_cast<LogoIntro*> (editor.getChildComponent (child)))
                return candidate;
        return nullptr;
    };
    auto findActivation = [] (juce::Component& editor) -> ActivationPanel*
    {
        for (int child = 0; child < editor.getNumChildComponents(); ++child)
            if (auto* candidate = dynamic_cast<ActivationPanel*> (editor.getChildComponent (child)))
                return candidate;
        return nullptr;
    };
    auto clickButton = [] (juce::Component& parent, const juce::String& text)
    {
        for (int child = 0; child < parent.getNumChildComponents(); ++child)
            if (auto* button = dynamic_cast<juce::TextButton*> (parent.getChildComponent (child)))
                if (button->getButtonText() == text)
                    button->triggerClick();
    };
    auto playingWhere = [&waitUntil] (LogoIntro& intro, const char* condition)
    {
        auto* browser = dynamic_cast<juce::WebBrowserComponent*> (intro.getChildComponent (0));
        if (browser == nullptr)
            return false;
        auto confirmed = std::make_shared<std::atomic<bool>> (false);
        const auto script = juce::String ("typeof video !== 'undefined' && video.readyState >= 2 && !video.paused && ")
                          + condition;
        return waitUntil ([&]
        {
            if (intro.isVisible() && ! confirmed->load())
                browser->evaluateJavascript (script, [confirmed] (juce::WebBrowserComponent::EvaluationResult result)
                {
                    if (const auto* value = result.getResult())
                        if ((bool) *value)
                            confirmed->store (true);
                });
            return confirmed->load();
        }, 9000);
    };
    auto noPlayerError = [] (juce::Component& editor)
    {
        for (int child = 0; child < editor.getNumChildComponents(); ++child)
            if (auto* label = dynamic_cast<juce::Label*> (editor.getChildComponent (child)))
                if (label->getText().startsWith ("Logo intro:"))
                    return false;
        return true;
    };

    DrumBreakerProcessor processor (options);
    juce::MemoryBlock savedProject;
    {
        DrumBreakerEditor editor (processor);
        editor.addToDesktop (0);
        editor.setVisible (true);
        auto* intro = findIntro (editor);
        auto* activation = findActivation (editor);
        check (intro != nullptr && activation != nullptr, "editor contains intro and activation overlays");
        if (intro == nullptr || activation == nullptr)
            return 1;
        check (waitUntil ([&] { return activation->isVisible(); }, 2000)
                && ! intro->isVisible() && intro->getNumChildComponents() == 0,
               "unverified visible editor prompts activation without creating a video player");
        juce::TextEditor* keyEditor = nullptr;
        for (int child = 0; child < activation->getNumChildComponents(); ++child)
            if (auto* candidate = dynamic_cast<juce::TextEditor*> (activation->getChildComponent (child)))
                keyEditor = candidate;
        check (keyEditor != nullptr, "activation panel exposes its license key field");
        if (keyEditor == nullptr)
            return 1;
        keyEditor->setText (licenseTest::key, true);
        clickButton (*activation, "ACTIVATE");
        check (waitUntil ([&] { return processor.isLicensed() && ! processor.getLicenseClient().getStatus().busy; }, 15000),
               "activating through the panel verifies the intro fixture");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        check (activation->isVisible() && ! intro->isVisible() && ! processor.hasPlayedLogoIntro(),
               "new activation waits for Continue before showing the intro");
        clickButton (*activation, "Continue");
        check (waitUntil ([&] { return intro->isVisible(); }, 3000),
               "Continue starts the intro in an activated editor");
        check (processor.hasPlayedLogoIntro(), "starting the intro marks this instance as played");
        check (intro->getBounds() == editor.getLocalBounds(), "intro fills the entire editor");
        editor.setSize (1000, 680);
        check (intro->getBounds() == editor.getLocalBounds(), "intro stays full-window after resize");
        check (! editor.keyPressed (juce::KeyPress ('Q', 0, 0)),
               "typing-piano input is not forwarded during intro playback");
        check (dynamic_cast<juce::WebBrowserComponent*> (intro->getChildComponent (0)) != nullptr,
               "intro creates its native embedded browser");
        check (playingWhere (*intro, "!video.muted"),
               "intro after a successful panel activation autoplays with sound");
        check (waitUntil ([&] { return ! intro->isVisible(); }, 15000)
                && intro->getNumChildComponents() == 0,
               "native playback completion returns to the GUI and releases its player");
        check (noPlayerError (editor), "native intro completes without a player error or watchdog timeout");
        check (editor.keyPressed (juce::KeyPress ('Q', 0, 0)),
               "natural completion restores typing-piano input in the GUI");
        editor.setVisible (false);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        editor.setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1000);
        check (! intro->isVisible(), "hiding and reopening the same editor does not replay the intro");
    }
    {
        DrumBreakerEditor reopened (processor);
        reopened.addToDesktop (0);
        reopened.setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1000);
        auto* intro = findIntro (reopened);
        check (intro != nullptr && ! intro->isVisible(), "a recreated editor on the same instance does not replay the intro");
    }
    processor.getStateInformation (savedProject);
    {
        DrumBreakerProcessor restored (options);
        restored.setStateInformation (savedProject.getData(), (int) savedProject.getSize());
        check (restored.hasPlayedLogoIntro(), "the played flag is saved with the project");
        restored.getLicenseClient().validateSaved();
        check (licenseTest::waitForIdle (restored.getLicenseClient()) && restored.isLicensed(),
               "restored project instance verifies the saved fixture license");
        DrumBreakerEditor editor (restored);
        editor.addToDesktop (0);
        editor.setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1000);
        auto* intro = findIntro (editor);
        check (intro != nullptr && ! intro->isVisible(), "reopening a saved project does not replay the intro");
    }
    {
        DrumBreakerProcessor second (options);
        second.getLicenseClient().validateSaved();
        check (licenseTest::waitForIdle (second.getLicenseClient()) && second.isLicensed(),
               "a new instance verifies the saved fixture license");
        DrumBreakerEditor editor (second);
        editor.addToDesktop (0);
        editor.setVisible (true);
        auto* intro = findIntro (editor);
        check (intro != nullptr && waitUntil ([&] { return intro->isVisible(); }, 3000),
               "a new plugin instance plays the intro once");
        if (intro == nullptr)
            return 1;
        check (playingWhere (*intro, "video.muted"),
               "embedded original MP4 actually decodes and autoplays muted without a new activation");
        if (auto* browser = dynamic_cast<juce::WebBrowserComponent*> (intro->getChildComponent (0)))
            browser->evaluateJavascript (
                "document.dispatchEvent(new PointerEvent('pointerdown', {bubbles: true}))");
        check (waitUntil ([&] { return ! intro->isVisible(); }, 2000)
                && intro->getNumChildComponents() == 0,
               "click-to-skip stops playback and destroys the player without click-through");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        check (! intro->isVisible(), "skipped intro does not replay while editor remains open");
        check (editor.keyPressed (juce::KeyPress ('Q', 0, 0)),
               "click-to-skip restores typing-piano input in the GUI");
    }
    {
        DrumBreakerProcessor third (options);
        third.getLicenseClient().validateSaved();
        check (licenseTest::waitForIdle (third.getLicenseClient()) && third.isLicensed(),
               "a third instance verifies the saved fixture license");
        DrumBreakerEditor editor (third);
        editor.addToDesktop (0);
        editor.setVisible (true);
        auto* intro = findIntro (editor);
        check (intro != nullptr && waitUntil ([&] { return intro->isVisible(); }, 3000),
               "a third instance starts its intro");
        reject.store (true);
        third.getLicenseClient().validateSaved();
        check (licenseTest::waitForIdle (third.getLicenseClient()) && ! third.isLicensed(),
               "synthetic revalidation revokes the fixture");
        check (intro != nullptr && waitUntil ([&] { return ! intro->isVisible(); }, 2000),
               "loss of license verification stops an active intro");
    }
    std::cout << "Logo intro checks: " << (failures == 0 ? "PASS" : "FAIL")
              << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialise;
    if (argc == 2 && juce::String (argv[1]) == "--logo-intro-self-test")
        return runLogoIntroTest();
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
    check (fresh.getName() == "DrumBreaker" && juce::String (JucePlugin_Name) == "DrumBreaker",
           "processor and plugin metadata use the DrumBreaker product name");
    check (juce::String (JucePlugin_Manufacturer) == "Hidden Audio",
           "host-facing plugin vendor is Hidden Audio");
    {
        auto audio = std::make_shared<juce::AudioBuffer<float>> (1, 4000);
        for (int sample = 0; sample < audio->getNumSamples(); ++sample)
            audio->setSample (0, sample, 0.5f * std::sin ((float) sample * 0.05f));
        BreakData::Ptr source = new BreakData();
        source->audio = audio;
        source->sourceSampleRate = 44100.0;
        source->slices = { Slice { 0, 1000 }, Slice { 1000, 2000 }, Slice { 2000, 4000 } };
        source->arrangement = { 0, 1, 2 };
        source->rebuildTimeline();
        BreakData::Ptr empty = new BreakData();
        empty->audio = audio;
        empty->sourceSampleRate = 44100.0;
        empty->slices = source->slices;
        empty->rebuildTimeline();

        WaveformEditor target;
        target.setCompactMode (true);
        target.setSize (600, 100);
        target.setFixedPixelsPerSample (0.1);
        target.setData (empty);
        const auto background = target.createComponentSnapshot (target.getLocalBounds()).getPixelAt (100, 20);
        target.setIncomingFragment (source, 2, 10.0f);
        check (juce::approximatelyEqual (target.getIncomingLength(), 2000.0),
               "an empty new break accepts the first hovering fragment");
        check (target.createComponentSnapshot (target.getLocalBounds()).getPixelAt (100, 20) != background,
               "the first fragment dragged over an empty new break draws its ghost");

        target.clearIncomingFragment();
        target.setData (empty->withArrangement ({ 0, 1 }));
        const auto withoutGap = target.createComponentSnapshot (target.getLocalBounds()).getPixelAt (380, 30);
        target.setIncomingFragment (source, 2, 110.0f);
        check (target.createComponentSnapshot (target.getLocalBounds()).getPixelAt (380, 30) != withoutGap,
               "hovering between fragments shifts later fragments right to open a gap");
        check (target.getPreferredContentWidth (2000.0 + target.getIncomingLength(), 1, 1000)
                > target.getPreferredContentWidth (2000.0, 1, 1000),
               "the hovering fragment widens the preferred display width");
        target.clearIncomingFragment();
        check (juce::approximatelyEqual (target.getIncomingLength(), 0.0)
                && target.createComponentSnapshot (target.getLocalBounds()).getPixelAt (380, 30) == withoutGap,
               "leaving the display closes the temporary gap");
    }
    check (! fresh.hasPlayedLogoIntro(), "fresh instances have not played the logo intro");
    {
        juce::MemoryBlock unplayed;
        fresh.getStateInformation (unplayed);
        DrumBreakerProcessor fromUnplayed (licensingOptions);
        fromUnplayed.setStateInformation (unplayed.getData(), (int) unplayed.getSize());
        check (! fromUnplayed.hasPlayedLogoIntro(), "projects saved before the intro still allow it once");

        DrumBreakerProcessor played (licensingOptions);
        played.markLogoIntroPlayed();
        juce::MemoryBlock playedState;
        played.getStateInformation (playedState);
        DrumBreakerProcessor reopened (licensingOptions);
        reopened.setStateInformation (playedState.getData(), (int) playedState.getSize());
        check (reopened.hasPlayedLogoIntro(), "reopened projects remember that the intro already played");
        reopened.setStateInformation (unplayed.getData(), (int) unplayed.getSize());
        check (reopened.hasPlayedLogoIntro(), "loading older state never re-arms an intro that already played");
        std::unique_ptr<juce::XmlElement> xml (juce::AudioProcessor::getXmlFromBinary (
            playedState.getData(), (int) playedState.getSize()));
        check (xml != nullptr && xml->getChildByName ("INTRO") != nullptr
                && ! reopened.apvts.state.getChildWithName ("INTRO").isValid(),
               "intro flag is stored beside, not inside, the parameter tree");
    }
    {
        PianoRoll piano (fresh.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
        constexpr char lower[] = "ZSXDCVGBHNJM";
        constexpr char upper[] = "Q2W3ER5T6Y7UI9O0P";
        for (int offset = 0; offset < 17; ++offset)
        {
            check (PianoRoll::noteForTypingKey (upper[offset]) == 60 + offset
                    && piano.keyPressed (juce::KeyPress (upper[offset], 0, 0)),
                   "FL upper typing row maps chromatically from middle C/MIDI 60");
            if (offset < 12)
                check (PianoRoll::noteForTypingKey (lower[offset]) == 48 + offset
                        && piano.keyPressed (juce::KeyPress (lower[offset], 0, 0)),
                       "FL lower typing row maps chromatically from C4/MIDI 48");
        }
        check (PianoRoll::noteForTypingKey ('q') == 60 && PianoRoll::noteForTypingKey ('z') == 48
                && PianoRoll::noteForTypingKey ('s') == 49,
               "Q/Z/S are calibrated to middle C, C4 and C#4 independent of case");
        check (! piano.keyPressed (juce::KeyPress ('Q', juce::ModifierKeys::ctrlModifier, 0))
                && ! piano.keyPressed (juce::KeyPress ('A', 0, 0)),
               "typing piano leaves shortcut modifiers and unmapped keys alone");
    }
    check (ToneStack::isFilterPadCentred (
        fresh.apvts.getRawParameterValue (params::filterCutoff)->load(),
        fresh.apvts.getRawParameterValue (params::filterReso)->load()),
        "fresh instances start at the neutral filter pad center");
    {
        XYPad pad (fresh.apvts, params::filterCutoff, params::filterReso);
        pad.setBounds (0, 0, 216, 216);
        int centreCallbacks = 0;
        pad.onCentre = [&centreCallbacks] { ++centreCallbacks; };
        auto eventAt = [&pad] (juce::Point<float> position, int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position,
                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pad, &pad, now, position, now, clicks, false);
        };
        for (const auto position : { juce::Point<float> (58.0f, 58.0f), juce::Point<float> (158.0f, 58.0f),
                                     juce::Point<float> (58.0f, 158.0f), juce::Point<float> (158.0f, 158.0f) })
        {
            const auto event = eventAt (position);
            pad.mouseDown (event);
            pad.mouseUp (event);
            const float x = fresh.apvts.getParameter (params::filterCutoff)->getValue();
            const float y = fresh.apvts.getParameter (params::filterReso)->getValue();
            check (std::abs (x - (position.x - 8.0f) / 200.0f) < 0.001f
                    && std::abs (y - (208.0f - position.y) / 200.0f) < 0.001f,
                   "each pad quadrant simultaneously controls cutoff and resonance independently");
        }
        const auto nearCentre = eventAt ({ 109.0f, 107.0f });
        pad.mouseDown (nearCentre);
        pad.mouseUp (nearCentre);
        check (centreCallbacks == 1 && ToneStack::isFilterPadCentred (
            fresh.apvts.getRawParameterValue (params::filterCutoff)->load(),
            fresh.apvts.getRawParameterValue (params::filterReso)->load()),
            "dragging into the center snap zone stores exactly neutral parameters");
        const auto singleClick = eventAt ({ 108.0f, 108.0f });
        pad.mouseDown (singleClick);
        pad.mouseUp (singleClick);
        pad.mouseDoubleClick (eventAt ({ 108.0f, 108.0f }, 2));
        check (ToneStack::isFilterPadCentred (
            fresh.apvts.getRawParameterValue (params::filterCutoff)->load(),
            fresh.apvts.getRawParameterValue (params::filterReso)->load()),
            "double-click recenter uses the same neutral parameters as dragging");
        pad.setEnabled (false);
        pad.mouseDown (eventAt ({ 158.0f, 58.0f }));
        check (ToneStack::isFilterPadCentred (
            fresh.apvts.getRawParameterValue (params::filterCutoff)->load(),
            fresh.apvts.getRawParameterValue (params::filterReso)->load()),
            "powered-off pad does not change its parameters");
    }
    juce::MemoryBlock centredState;
    fresh.getStateInformation (centredState);
    setParameter (fresh, params::filterCutoff, 500.0f);
    fresh.setStateInformation (centredState.getData(), (int) centredState.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    defaults (fresh);
    juce::MemoryBlock emptyState;
    fresh.getStateInformation (emptyState);

    DrumBreakerProcessor projectA (licensingOptions);
    juce::String error;
    check (projectA.loadSampleFile (sourceFile, error), "project A loads its sample");
    check (projectA.getBreakData() != nullptr && projectA.getBreakData()->arrangement == std::vector<int> { 0 }
            && ! projectA.hasBrokenSample(), "newly loaded sample starts in the restored, unbroken arrangement");
    projectA.breakSample();
    check (projectA.hasBrokenSample(), "breaking the loaded sample enables custom break creation");
    projectA.setArrangement ({ 0, 0 });
    setParameter (projectA, params::filterPreset, 1.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    setParameter (projectA, params::eqMidGain, 6.0f);
    setParameter (projectA, params::filterCutoff, 1234.0f);
    setParameter (projectA, params::filterMotion, (float) ToneStack::FilterMotion::lowPassSweep);
    setParameter (projectA, params::reverb, 0.3f);
    check (projectA.createCustomBreak(), "project A creates an empty custom break");
    check (projectA.getCustomBreakData() != nullptr
            && projectA.getCustomBreakData()->arrangement.empty(), "custom break starts empty");
    projectA.setCustomArrangement ({ 0, 0, 0 });
    setParameter (projectA, params::eqMidGain, -4.0f);
    setParameter (projectA, params::filterCutoff, 2500.0f);
    setParameter (projectA, params::filterMotion, (float) ToneStack::FilterMotion::bandPassSweep);
    projectA.selectCustomBreak (false);
    check (std::abs (projectA.apvts.getRawParameterValue (params::eqMidGain)->load() - 6.0f) < 0.01f
            && std::abs (projectA.apvts.getRawParameterValue (params::filterCutoff)->load() - 1234.0f) < 0.1f,
           "selecting the main break restores its independent EQ and filter settings");
    check ((int) projectA.apvts.getRawParameterValue (params::filterMotion)->load()
            == (int) ToneStack::FilterMotion::lowPassSweep,
           "main break remembers its own automated low-pass preset");
    projectA.selectCustomBreak (true);
    check (std::abs (projectA.apvts.getRawParameterValue (params::eqMidGain)->load() + 4.0f) < 0.01f
            && std::abs (projectA.apvts.getRawParameterValue (params::filterCutoff)->load() - 2500.0f) < 0.1f,
           "selecting the custom break restores its independent EQ and filter settings");
    check ((int) projectA.apvts.getRawParameterValue (params::filterMotion)->load()
            == (int) ToneStack::FilterMotion::bandPassSweep,
           "custom break remembers its independent automated band-pass preset");
    setParameter (projectA, params::pitch, 3.0f);
    projectA.setPitchWheelFromUI (0.75f);
    projectA.setModWheelFromUI (0.45f);
    juce::MemoryBlock savedA;
    projectA.getStateInformation (savedA);

    for (bool customSelected : { false, true })
    {
        licenseTest::FixtureStore restoreStore;
        DrumBreakerProcessor restoreTarget (restoreStore.options());
        check (restoreTarget.getLicenseClient().activate (licenseTest::key)
                && licenseTest::waitForIdle (restoreTarget.getLicenseClient())
                && restoreTarget.isLicensed(), "synthetic activation enables restore playback/export checks");
        restoreTarget.setStateInformation (savedA.getData(), (int) savedA.getSize());
        restoreTarget.selectCustomBreak (customSelected);
        const auto original = restoreTarget.getBreakData();
        const auto custom = restoreTarget.getCustomBreakData();
        const auto customLength = custom->totalLength;
        const auto customStarts = custom->starts;
        restoreTarget.restoreWholeSample();
        const auto whole = restoreTarget.getBreakData();
        check (! restoreTarget.hasBrokenSample() && whole->audio == original->audio
                && whole->slices.size() == 1 && whole->slices.front().start == 0
                && whole->slices.front().end == original->audio->getNumSamples()
                && whole->arrangement == std::vector<int> { 0 }
                && whole->totalLength == original->audio->getNumSamples()
                && whole->starts == std::vector<juce::int64> { 0 },
               "RESTORE rebuilds the whole original waveform even when NEW BREAK is selected");
        check (restoreTarget.getCustomBreakData() == custom
                && custom->arrangement == std::vector<int> { 0, 0, 0 }
                && custom->totalLength == customLength && custom->starts == customStarts
                && restoreTarget.isCustomBreakSelected() == customSelected,
               "RESTORE leaves NEW BREAK pieces, timeline, and display selection unchanged");
        check (restoreTarget.captureExportSnapshot().data == (customSelected ? custom : whole),
               "RESTORE keeps playback and export on the selected arrangement");

        juce::MemoryBlock wholeState;
        restoreTarget.getStateInformation (wholeState);
        DrumBreakerProcessor reopened (licensingOptions);
        reopened.setStateInformation (wholeState.getData(), (int) wholeState.getSize());
        check (! reopened.hasBrokenSample()
                && reopened.getBreakData()->totalLength == original->audio->getNumSamples()
                && reopened.getCustomBreakData()->arrangement == custom->arrangement
                && reopened.getCustomBreakData()->totalLength == customLength,
               "saving after RESTORE preserves the whole original and existing NEW BREAK");

        DrumBreakerEditor editor (restoreTarget);
        std::vector<WaveformEditor*> displays;
        for (int child = 0; child < editor.getNumChildComponents(); ++child)
            if (auto* display = dynamic_cast<WaveformEditor*> (editor.getChildComponent (child)))
                displays.push_back (display);
        check (displays.size() == 2, "RESTORE keeps both waveform displays");
        if (displays.size() == 2)
        {
            auto* sourceDisplay = displays[0];
            auto* customDisplay = displays[1];
            auto point = customDisplay->getBounds().getCentre();
            check (customDisplay->isVisible()
                    && ! sourceDisplay->onFragmentDragged (whole, 0, point)
                    && ! sourceDisplay->onFragmentDropped (whole, 0, point)
                    && customDisplay->getIncomingLength() == 0.0
                    && restoreTarget.getCustomBreakData() == custom,
                   "restored whole sample cannot be hovered or dropped into NEW BREAK before BREAK");

            restoreTarget.breakSample();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            const auto broken = restoreTarget.getBreakData();
            check (restoreTarget.hasBrokenSample() && broken->totalLength == whole->totalLength
                    && restoreTarget.getCustomBreakData() == custom,
                   "BREAK rebuilds the original slices without replacing existing NEW BREAK pieces");
            point = customDisplay->getBounds().getCentre();
            check (sourceDisplay->onFragmentDropped (broken, 0, point)
                    && restoreTarget.getCustomBreakData()->arrangement.size() == 4
                    && restoreTarget.getCustomBreakData()->totalLength
                        == customLength + broken->slices.front().length(),
                   "BREAK allows more fragments to be added to the existing NEW BREAK");
        }
        restoreTarget.selectCustomBreak (false);
        check (std::abs (restoreTarget.apvts.getRawParameterValue (params::eqMidGain)->load() - 6.0f) < 0.01f,
               "RESTORE and BREAK preserve the original effect bank");
        restoreTarget.selectCustomBreak (true);
        check (std::abs (restoreTarget.apvts.getRawParameterValue (params::eqMidGain)->load() + 4.0f) < 0.01f
                && (int) restoreTarget.apvts.getRawParameterValue (params::filterMotion)->load()
                    == (int) ToneStack::FilterMotion::bandPassSweep,
               "RESTORE and BREAK preserve NEW BREAK effects");
    }

    DrumBreakerProcessor projectB (licensingOptions);
    defaults (projectB);
    check (projectA.hasSample(), "creating project B leaves project A untouched");

    DrumBreakerProcessor restored (licensingOptions);
    restored.setStateInformation (savedA.getData(), (int) savedA.getSize());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    check (restored.hasSample() && restored.getBreakData()->name == "session.wav", "saved project restores its sample");
    check (restored.getBreakData() != nullptr
            && restored.getBreakData()->arrangement == std::vector<int> { 0, 0 }, "saved project restores its arrangement");
    check (restored.hasCustomBreak() && restored.isCustomBreakSelected()
            && restored.getCustomBreakData()->arrangement == std::vector<int> { 0, 0, 0 },
           "saved project restores the custom break arrangement and selected display");
    {
        auto legacyXml = juce::AudioProcessor::getXmlFromBinary (savedA.getData(), (int) savedA.getSize());
        auto* banks = legacyXml != nullptr ? legacyXml->getChildByName ("EFFECT_BANKS") : nullptr;
        check (banks != nullptr, "saved state contains filter effect banks");
        if (banks != nullptr)
        {
            banks->removeAttribute ("filterMotionChoices");
            for (const auto* name : { "main", "custom" })
            {
                auto values = juce::StringArray::fromTokens (banks->getStringAttribute (name), ",", "");
                values.set (values.size() - 1, juce::String (name) == "main" ? "0.33333333" : "0.66666667");
                banks->setAttribute (name, values.joinIntoString (","));
            }
            for (auto* parameter : legacyXml->getChildIterator())
                if (parameter->getStringAttribute ("id") == params::filterMotion)
                    parameter->setAttribute ("value", 2.0);
            juce::MemoryBlock legacyState;
            juce::AudioProcessor::copyXmlToBinary (*legacyXml, legacyState);
            DrumBreakerProcessor legacy (licensingOptions);
            legacy.setStateInformation (legacyState.getData(), (int) legacyState.getSize());
            check ((int) legacy.apvts.getRawParameterValue (params::filterMotion)->load()
                    == (int) ToneStack::FilterMotion::highPassSweep,
                   "older saved custom banks retain their high-pass preset after adding band-pass");
            legacy.selectCustomBreak (false);
            check ((int) legacy.apvts.getRawParameterValue (params::filterMotion)->load()
                    == (int) ToneStack::FilterMotion::lowPassSweep,
                   "older saved main banks retain their low-pass preset");
        }
    }
    for (int opening = 0; opening < 2; ++opening)
    {
        DrumBreakerEditor editor (restored);
        juce::ComboBox* automationBox = nullptr;
        for (int child = 0; child < editor.getNumChildComponents(); ++child)
            if (auto* box = dynamic_cast<juce::ComboBox*> (editor.getChildComponent (child)))
                if (box->getItemText (0) == "Select preset...")
                    automationBox = box;
        check (automationBox != nullptr, "editor has the automated filter preset dropdown");
        if (automationBox != nullptr)
        {
            check (automationBox->getNumItems() == 4 && automationBox->getItemText (1) == "Low Pass"
                    && automationBox->getItemText (2) == "Band Pass" && automationBox->getItemText (3) == "High Pass"
                    && automationBox->getSelectedId() == 3,
                   "dropdown has only placeholder and three sweeps and restores the custom band-pass selection");
            restored.selectCustomBreak (false);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            check (automationBox->getSelectedId() == 2, "dropdown follows the main break's remembered low-pass selection");
            restored.selectCustomBreak (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            check (automationBox->getSelectedId() == 3, "dropdown follows the custom break's remembered band-pass selection");
        }
        std::vector<WaveformEditor*> displays;
        for (int child = 0; child < editor.getNumChildComponents(); ++child)
            if (auto* display = dynamic_cast<WaveformEditor*> (editor.getChildComponent (child)))
                displays.push_back (display);
        check (displays.size() == 2, "reopened editor contains both waveform displays");
        if (displays.size() == 2)
        {
            for (const auto size : { juce::Point<int> (1060, 680), juce::Point<int> (900, 600),
                                     juce::Point<int> (1800, 1200) })
            {
                editor.setSize (size.x, size.y);
                const auto original = displays[0]->getBounds();
                const auto custom = displays[1]->getBounds();
                check (displays[1]->isVisible() && custom.getY() == original.getBottom() + 6,
                       "restored custom waveform stays below the original on reopen and resize");
                check (custom.getHeight() == original.getHeight()
                        && std::abs (custom.getCentreX() - original.getCentreX()) <= 1,
                       "custom waveform matches the original height and remains horizontally centred");
            }
        }
    }
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
    check (std::abs (restored.apvts.getRawParameterValue (params::eqMidGain)->load() + 4.0f) < 0.01f,
           "queued preset from previous state cannot overwrite restored preferences");
    restored.selectCustomBreak (false);
    check (std::abs (restored.apvts.getRawParameterValue (params::eqMidGain)->load() - 6.0f) < 0.01f
            && std::abs (restored.apvts.getRawParameterValue (params::filterCutoff)->load() - 1234.0f) < 0.1f,
           "restored main display retains its own EQ and filter settings");

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
        {
            DrumBreakerEditor editor (gated);
            check (editor.getWantsKeyboardFocus() && editor.keyPressed (juce::KeyPress ('Q', 0, 0))
                    && ! editor.keyPressed (juce::KeyPress ('Q', juce::ModifierKeys::ctrlModifier, 0)),
                   "opened editor routes FL typing keys without consuming host shortcuts");
            juce::ComboBox* automationBox = nullptr;
            for (int child = 0; child < editor.getNumChildComponents(); ++child)
                if (auto* box = dynamic_cast<juce::ComboBox*> (editor.getChildComponent (child)))
                    if (box->getItemText (0) == "Select preset...")
                        automationBox = box;
            for (const auto motion : { ToneStack::FilterMotion::lowPassSweep, ToneStack::FilterMotion::bandPassSweep,
                                       ToneStack::FilterMotion::highPassSweep })
            {
                setParameter (gated, params::filterEnabled, 1.0f);
                if (automationBox != nullptr)
                {
                    const int item = motion == ToneStack::FilterMotion::lowPassSweep ? 2
                        : motion == ToneStack::FilterMotion::bandPassSweep ? 3 : 4;
                    automationBox->setSelectedId (item, juce::sendNotificationSync);
                    check ((int) gated.apvts.getRawParameterValue (params::filterMotion)->load() == (int) motion,
                           "each dropdown selection activates the matching automated filter");
                    check (automationBox->isEnabled(), "dropdown stays usable during automated filtering");
                }
                else
                    check (false, "automated filter dropdown exists in the licensed editor");
                setParameter (gated, params::filterCutoff, 1500.0f);
                setParameter (gated, params::filterReso, 0.707f);
                midi.clear();
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                for (int block = 0; block < 100; ++block)
                {
                    gated.processBlock (output, midi);
                    midi.clear();
                }
                if (automationBox != nullptr)
                {
                    automationBox->setSelectedId (1, juce::sendNotificationSync);
                    check (gated.apvts.getRawParameterValue (params::filterMotion)->load() == 0.0f,
                           "Select preset placeholder returns to manual filtering");
                }
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                XYPad* pad = nullptr;
                PianoRoll* piano = nullptr;
                for (int child = 0; child < editor.getNumChildComponents(); ++child)
                {
                    if (auto* candidate = dynamic_cast<XYPad*> (editor.getChildComponent (child)))
                        pad = candidate;
                    if (auto* candidate = dynamic_cast<PianoRoll*> (editor.getChildComponent (child)))
                        piano = candidate;
                }
                check (pad != nullptr && piano != nullptr, "editor contains the live pad and MIDI keyboard");
                check (gated.keyboardState.isNoteOn (1, 60), "DAW piano-roll MIDI updates the displayed keyboard state");
                if (pad != nullptr)
                {
                    juce::Image image (juce::Image::ARGB, pad->getWidth(), pad->getHeight(), true);
                    {
                        juce::Graphics graphics (image);
                        pad->paint (graphics);
                    }
                    const auto area = pad->getLocalBounds().toFloat().reduced (8.0f);
                    const float x = gated.apvts.getParameter (params::filterCutoff)->convertTo0to1 (gated.getLiveFilterCutoff());
                    const float y = gated.apvts.getParameter (params::filterReso)->convertTo0to1 (gated.getLiveFilterResonance());
                    const auto expectedThumb = juce::Point<float> (area.getX() + area.getWidth() * x,
                                                                   area.getBottom() - area.getHeight() * y).roundToInt();
                    check (image.getPixelAt (expectedThumb.x, expectedThumb.y) == emerald::accentBright,
                           "all three automation presets draw the pad handle at actual live cutoff/resonance");
                    check (std::abs (gated.apvts.getRawParameterValue (params::filterCutoff)->load() - 1500.0f) < 0.01f
                            && std::abs (gated.apvts.getRawParameterValue (params::filterReso)->load() - 0.707f) < 0.01f,
                           "moving automation display does not overwrite the stored manual pad values");
                }
                midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                gated.processBlock (output, midi);
                check (! gated.keyboardState.isNoteOn (1, 60), "DAW note release clears the displayed key");
            }
            setParameter (gated, params::filterMotion, 0.0f);
            gated.keyboardState.reset();
            gated.prepareToPlay (44100.0, 512);
            midi.clear();
        }
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