#include "Analysis/SampleAnalyzer.h"
#include "Audio/ToneStack.h"
#include "Audio/OfflineExporter.h"
#include "ParameterIDs.h"
#include "Licensing/LicensePolicy.h"
#include "Licensing/LicenseClient.h"
#include "LicenseTestSupport.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <iomanip>
#include <cmath>

static int runLicensePolicyChecks()
{
    using namespace drumbreaker::licensing;
    int failures = 0;
    auto makeReply = []
    {
        return juce::JSON::parse (R"({
            "activated":true,"valid":true,"error":null,
            "license_key":{"status":"active","key":"fixture-only-not-a-customer-key","expires_at":null},
            "instance":{"id":"fixture-instance","name":"fixture-device"},
            "meta":{"product_id":1412490,"variant_id":2205337}
        })");
    };
    const auto now = juce::Time::fromISO8601 ("2026-10-05T12:00:00Z");
    auto check = [&] (juce::var reply, bool expected, const char* name,
                      ReplyKind kind = ReplyKind::activation, int status = 200)
    {
        const auto result = checkReply (status, juce::JSON::toString (reply), kind,
            "fixture-only-not-a-customer-key", "fixture-device", "fixture-instance", now);
        const auto expectedId = kind == ReplyKind::preflight ? juce::String() : juce::String ("fixture-instance");
        if (result.result.wasOk() != expected || (expected && result.instanceId != expectedId)
            || (! expected && result.instanceId.isNotEmpty()))
        {
            std::cerr << "FAILED: " << name << "\n";
            ++failures;
        }
    };
    check (makeReply(), true, "matching activation is accepted");
    check (makeReply(), true, "matching saved instance validation is accepted", ReplyKind::validation);
    check (makeReply(), false, "HTTP errors cannot unlock", ReplyKind::activation, 500);
    check (juce::var(), false, "empty response cannot unlock");
    check (juce::JSON::parse ("[]"), false, "array response cannot unlock");
    auto reply = makeReply();
    reply.getProperty ("meta", {}).getDynamicObject()->setProperty ("product_id", 1);
    check (reply, false, "another product cannot unlock");
    reply = makeReply();
    reply.getProperty ("meta", {}).getDynamicObject()->setProperty ("variant_id", 1);
    check (reply, false, "another variant cannot unlock");
    reply = makeReply();
    reply.getProperty ("meta", {}).getDynamicObject()->setProperty ("product_id", "1412490");
    check (reply, false, "unexpected metadata types cannot unlock");
    for (const char* status : { "inactive", "expired", "disabled" })
    {
        reply = makeReply();
        reply.getProperty ("license_key", {}).getDynamicObject()->setProperty ("status", status);
        check (reply, false, "non-active license cannot unlock");
    }
    reply = makeReply();
    reply.getDynamicObject()->setProperty ("activated", "true");
    check (reply, false, "string success flags cannot unlock");
    reply = makeReply();
    reply.getDynamicObject()->setProperty ("activated", false);
    check (reply, false, "activation-limit rejection cannot unlock");
    reply = makeReply();
    reply.getDynamicObject()->setProperty ("valid", false);
    check (reply, false, "invalid saved license cannot unlock", ReplyKind::validation);
    reply = makeReply();
    reply.getDynamicObject()->setProperty ("error", "fixture failure");
    check (reply, false, "service error cannot unlock");
    reply = makeReply();
    reply.getProperty ("license_key", {}).getDynamicObject()->setProperty ("key", "wrong-fixture-key");
    check (reply, false, "mismatched key cannot unlock");
    reply = makeReply();
    reply.getDynamicObject()->setProperty ("instance", juce::var());
    check (reply, false, "validation without an instance cannot unlock", ReplyKind::validation);
    reply = makeReply();
    reply.getProperty ("instance", {}).getDynamicObject()->setProperty ("name", "other-device");
    check (reply, false, "another device cannot unlock");
    reply = makeReply();
    reply.getProperty ("instance", {}).getDynamicObject()->setProperty ("id", "another-instance");
    check (reply, false, "another saved instance cannot unlock", ReplyKind::validation);
    reply = makeReply();
    reply.getProperty ("license_key", {}).getDynamicObject()->setProperty ("expires_at", "2020-01-01T00:00:00Z");
    check (reply, false, "expired date cannot unlock");
    reply = makeReply();
    reply.getProperty ("license_key", {}).getDynamicObject()->setProperty ("expires_at", "invalid");
    check (reply, false, "malformed expiration cannot unlock");
    reply = makeReply();
    reply.getProperty ("license_key", {}).getDynamicObject()->setProperty ("expires_at", "2099-01-01T00:00:00Z");
    check (reply, true, "future expiration is accepted");

    reply = makeReply();
    reply.getProperty ("license_key", {}).getDynamicObject()->setProperty ("status", "inactive");
    reply.getDynamicObject()->setProperty ("instance", juce::var());
    check (reply, true, "unactivated matching key passes product preflight", ReplyKind::preflight);

    auto expect = [&failures] (bool passed, const char* name)
    {
        if (! passed)
        {
            std::cerr << "FAILED: " << name << "\n";
            ++failures;
        }
    };
    int requests = 0, activations = 0;
    auto transport = [&] (const LicenseRequest& request)
    {
        ++requests;
        expect (request.key == "fixture-only-not-a-customer-key" && request.device == "fixture-device",
                "request uses the supplied key/device");
        auto body = makeReply();
        if (request.kind == ReplyKind::preflight)
        {
            expect (request.instance.isEmpty(), "preflight does not claim an existing activation");
            body.getDynamicObject()->setProperty ("instance", juce::var());
            body.getProperty ("license_key", {}).getDynamicObject()->setProperty ("status", "inactive");
        }
        if (request.kind == ReplyKind::activation)
            ++activations;
        if (request.kind == ReplyKind::validation)
            expect (request.instance == "fixture-instance", "existing activation uses the saved instance ID");
        return LicenseResponse { juce::Result::ok(), 200, juce::JSON::toString (body) };
    };
    auto activated = requestActivation (" fixture-only-not-a-customer-key ", "fixture-device", {}, transport);
    expect (activated.result.wasOk() && activated.instanceId == "fixture-instance"
            && requests == 2 && activations == 1, "fresh activation preflights first and activates once");
    requests = 0;
    activations = 0;
    auto validated = requestActivation ("fixture-only-not-a-customer-key", "fixture-device", "fixture-instance", transport);
    expect (validated.result.wasOk() && requests == 1 && activations == 0,
            "existing instance validates without consuming another activation");

    requests = 0;
    auto wrongProduct = requestActivation ("fixture-only-not-a-customer-key", "fixture-device", {},
        [&] (const LicenseRequest& request)
        {
            ++requests;
            expect (request.kind == ReplyKind::preflight, "wrong product never reaches activation");
            auto body = makeReply();
            body.getProperty ("meta", {}).getDynamicObject()->setProperty ("product_id", 1);
            return LicenseResponse { juce::Result::ok(), 200, juce::JSON::toString (body) };
        });
    expect (wrongProduct.result.failed() && requests == 1, "wrong product rejected before consuming a slot");

    requests = 0;
    auto networkFailure = requestActivation ("fixture-only-not-a-customer-key", "fixture-device", {},
        [&] (const LicenseRequest&)
        {
            ++requests;
            return LicenseResponse { juce::Result::fail ("fixture timeout"), 0, {} };
        });
    expect (networkFailure.result.failed() && requests == 1, "network failure is not retried or activated");

    requests = 0;
    auto uncertainActivation = requestActivation ("fixture-only-not-a-customer-key", "fixture-device", {},
        [&] (const LicenseRequest& request)
        {
            ++requests;
            if (request.kind == ReplyKind::activation)
                return LicenseResponse { juce::Result::fail ("fixture uncertain response"), 0, {} };
            return LicenseResponse { juce::Result::ok(), 200, juce::JSON::toString (makeReply()) };
        });
    expect (uncertainActivation.result.failed() && uncertainActivation.instanceId.isEmpty() && requests == 2,
            "uncertain activation is not automatically retried");
    auto rejectedActivation = requestActivation ("fixture-only-not-a-customer-key", "fixture-device", {},
        [&] (const LicenseRequest& request)
        {
            auto body = makeReply();
            if (request.kind == ReplyKind::activation)
                body.getDynamicObject()->setProperty ("activated", false);
            return LicenseResponse { juce::Result::ok(), 200, juce::JSON::toString (body) };
        });
    expect (rejectedActivation.result.failed() && rejectedActivation.instanceId.isEmpty(),
            "activation-limit rejection never claims success");
    std::cout << "License policy checks: " << (failures == 0 ? "PASS" : "FAIL") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 6;
}

static int runLicensePersistenceChecks()
{
    using namespace drumbreaker::licensing;
    int failures = 0;
    auto expect = [&failures] (bool passed, const char* description)
    {
        if (! passed)
        {
            std::cerr << "FAILED: " << description << "\n";
            ++failures;
        }
    };
    const auto scope = "persistence-test-" + juce::Uuid().toString();
    LicenseStore store (scope);
    struct Cleanup
    {
        LicenseStore& store;
        ~Cleanup() { store.remove(); }
    } cleanup { store };
    StoredActivation loaded;
    bool found = false;
    expect (store.load (loaded, found).wasOk() && ! found, "new protected test scope is empty");
    const StoredActivation fixture { "fixture-only-not-a-customer-key", "fixture-device", "fixture-instance" };
    if (store.save (fixture).failed())
    {
        std::cerr << "FAILED: OS-protected test record cannot be saved\n";
        return 7;
    }
    LicenseStore reopened (scope);
    expect (reopened.load (loaded, found).wasOk() && found && loaded.key == fixture.key
            && loaded.device == fixture.device && loaded.instance == fixture.instance,
            "new store object restores protected key/device/instance across lifetimes");
    expect (store.save ({ fixture.key, fixture.device, {} }).failed(), "incomplete activation cannot replace protected data");
    expect (store.remove().wasOk() && store.load (loaded, found).wasOk() && ! found,
            "protected synthetic record can be removed safely");

    auto waitForIdle = [] (LicenseClient& client)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
        juce::WaitableEvent pause;
        while (client.getStatus().busy)
        {
            if (juce::Time::getMillisecondCounterHiRes() >= deadline)
                return false;
            pause.wait (5);
        }
        return true;
    };
    std::atomic<int> preflights { 0 }, activations { 0 }, validations { 0 };
    LicenseClient::Options options;
    options.storageScope = scope;
    options.restoreOnStartup = false;
    options.transport = [&] (const LicenseRequest& request)
    {
        if (request.kind == ReplyKind::activation) ++activations;
        if (request.kind == ReplyKind::validation) ++validations;
        if (request.kind == ReplyKind::preflight) ++preflights;
        auto reply = juce::JSON::parse (R"({
            "activated":true,"valid":true,"error":null,
            "license_key":{"status":"active","key":"fixture-only-not-a-customer-key","expires_at":null},
            "instance":{"id":"fixture-instance","name":"fixture-device"},
            "meta":{"product_id":1412490,"variant_id":2205337}
        })");
        if (request.kind == ReplyKind::preflight)
            reply.getDynamicObject()->setProperty ("instance", juce::var());
        return LicenseResponse { juce::Result::ok(), 200, juce::JSON::toString (reply) };
    };
    {
        LicenseClient first (fixture.device, options);
        LicenseClient second (fixture.device, options);
        expect (first.activate (fixture.key) && second.activate (fixture.key), "two new clients accept coordinated activation requests");
        expect (waitForIdle (first) && waitForIdle (second), "coordinated activation requests finish");
        expect (first.getStatus().verified && first.getStatus().saved
                && second.getStatus().verified && second.getStatus().saved,
                "both clients share a securely saved and validated activation");
        expect (preflights.load() == 1 && activations.load() == 1 && validations.load() == 1,
                "simultaneous clients consume one new activation, not two");
    }
    const int previousActivations = activations.load();
    const int previousValidations = validations.load();
    options.restoreOnStartup = true;
    {
        LicenseClient restarted (fixture.device, options);
        expect (waitForIdle (restarted) && restarted.getStatus().verified && restarted.getStatus().saved,
                "new client restores and validates saved activation on startup");
        expect (activations.load() == previousActivations && validations.load() == previousValidations + 1,
                "restart validates the same instance without consuming another activation");
    }
    auto offline = options;
    std::atomic<int> offlineCalls { 0 }, offlineActivations { 0 };
    offline.transport = [&] (const LicenseRequest& request)
    {
        ++offlineCalls;
        if (request.kind == ReplyKind::activation) ++offlineActivations;
        return LicenseResponse { juce::Result::fail ("fixture offline"), 0, {} };
    };
    {
        LicenseClient restartedOffline (fixture.device, offline);
        expect (waitForIdle (restartedOffline) && ! restartedOffline.getStatus().verified
                && restartedOffline.getStatus().saved && offlineCalls.load() == 1 && offlineActivations.load() == 0,
                "offline restart retains saved credentials without claiming verification or activating again");
        expect (restartedOffline.validateSaved() && waitForIdle (restartedOffline)
            && offlineCalls.load() == 2 && offlineActivations.load() == 0,
            "retrying saved validation never requests an activation");
    }
    expect (store.load (loaded, found).wasOk() && found && loaded.instance == fixture.instance,
            "failed validation leaves the protected activation intact");
    auto revoked = options;
    std::atomic<int> revokedActivations { 0 };
    revoked.transport = [&] (const LicenseRequest& request)
    {
        if (request.kind == ReplyKind::activation) ++revokedActivations;
        return LicenseResponse { juce::Result::ok(), 200, R"({"valid":false,"error":"fixture revoked"})" };
    };
    {
        LicenseClient rejected (fixture.device, revoked);
        expect (waitForIdle (rejected) && ! rejected.getStatus().verified && revokedActivations.load() == 0,
                "rejected saved instance never falls back to allocating a fresh activation");
    }
    std::atomic<int> wrongDeviceRequests { 0 };
    auto differentDevice = options;
    differentDevice.transport = [&] (const LicenseRequest&)
    {
        ++wrongDeviceRequests;
        return LicenseResponse { juce::Result::fail ("should not be called"), 0, {} };
    };
    {
        LicenseClient mismatch ("other-fixture-device", differentDevice);
        expect (waitForIdle (mismatch) && ! mismatch.getStatus().verified && wrongDeviceRequests.load() == 0,
                "copied activation on a different device is rejected without network activation");
    }
    std::cout << "License persistence checks: " << (failures == 0 ? "PASS" : "FAIL") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 7;
}

static int runEQChecks()
{
    using drumbreaker::ToneStack;
    namespace params = drumbreaker::params;
    int failures = 0;
    auto check = [&failures] (bool result, const char* name)
    {
        if (! result)
        {
            std::cerr << "FAILED: " << name << "\n";
            ++failures;
        }
    };

    ToneStack::EQSettings settings;
    for (size_t band = 0; band < settings.size(); ++band)
    {
        settings[band].frequency = params::eqDefaultFrequency (band);
        check (std::abs (params::eqPositionAtFrequency (settings[band].frequency)
                         - (float) (band + 1) / 8.0f) < 0.00001f, "even logarithmic band positions");
    }
    check (std::abs (params::eqPositionAtFrequency (settings[3].frequency) - 0.5f) < 0.00001f,
           "band 4 is centered");
    check (std::abs (params::eqFrequencyAtPosition (0.0f) - 20.0f) < 0.01f,
           "left frequency limit");
    check (std::abs (params::eqFrequencyAtPosition (1.0f) - 20000.0f) < 0.01f,
           "right frequency limit");

    for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0 })
    {
        for (size_t band = 0; band < settings.size(); ++band)
        {
            const auto flat = ToneStack::makeEQCoefficients (band, settings[band], rate);
            for (double frequency : { 20.0, 100.0, 1000.0, rate * 0.45 })
                check (std::abs (flat->getMagnitudeForFrequency (frequency, rate) - 1.0) < 0.001,
                       "default EQ is unity gain");

            for (float gain : { -18.0f, 18.0f })
            {
                auto active = settings[band];
                active.gainDb = gain;
                const auto coefficients = ToneStack::makeEQCoefficients (band, active, rate);
                const double frequency = band == 0 ? 0.0 : band == 6 ? rate * 0.5
                    : juce::jmin ((double) active.frequency, rate * 0.49);
                const double measured = juce::Decibels::gainToDecibels (
                    coefficients->getMagnitudeForFrequency (frequency, rate));
                if (! std::isfinite (measured) || std::abs (measured - gain) >= 0.04)
                    std::cerr << "band=" << band + 1 << " rate=" << rate
                              << " target=" << gain << " measured=" << measured << "\n";
                check (std::isfinite (measured) && std::abs (measured - gain) < 0.04,
                       "all seven filters reach their gain limits");
            }
        }
    }

    for (size_t activeBand = 0; activeBand < settings.size(); ++activeBand)
    {
        for (float gain : { -12.0f, 12.0f })
        {
            auto active = settings;
            active[activeBand].gainDb = gain;
            constexpr double rate = 48000.0;
            const double frequency = active[activeBand].frequency;
            double expected = 1.0;
            for (size_t band = 0; band < active.size(); ++band)
                expected *= ToneStack::makeEQCoefficients (band, active[band], rate)
                                ->getMagnitudeForFrequency (frequency, rate);

            for (int channels : { 1, 2 })
            {
                ToneStack tone;
                tone.prepare ({ rate, 512, (juce::uint32) channels });
                tone.setEQ (active);
                tone.setFilter (ToneStack::FilterType::lowPass, 20000.0f, 0.707f, 1.0f, 0.0f, 0.0f);
                tone.setReverb (0.0f);
                juce::AudioBuffer<float> audio (channels, 512);
                double inputPower = 0.0;
                double outputPower = 0.0;
                for (int block = 0; block < 180; ++block)
                {
                    for (int sample = 0; sample < 512; ++sample)
                    {
                        const float input = (float) (0.05 * std::sin (
                            2.0 * juce::MathConstants<double>::pi * frequency * (block * 512 + sample) / rate));
                        for (int channel = 0; channel < channels; ++channel)
                            audio.setSample (channel, sample, input);
                        if (block >= 100)
                            inputPower += (double) input * input;
                    }
                    tone.process (audio);
                    for (int sample = 0; sample < 512; ++sample)
                    {
                        const float output = audio.getSample (0, sample);
                        check (std::isfinite (output), "processed audio stays finite");
                        if (block >= 100)
                            outputPower += (double) output * output;
                        if (channels == 2)
                            check (std::abs (output - audio.getSample (1, sample)) < 0.00001f,
                                   "stereo channels have matching responses");
                    }
                }
                const double measured = std::sqrt (outputPower / inputPower);
                check (std::abs (20.0 * std::log10 (measured / expected)) < 0.08,
                       "audio response matches displayed coefficients");
            }
        }
    }

    for (auto preset : { ToneStack::EQPreset::flat, ToneStack::EQPreset::lowPass,
                         ToneStack::EQPreset::bandPass, ToneStack::EQPreset::highPass })
    {
        const auto active = ToneStack::makeEQPreset (preset);
        auto magnitudeAt = [&active] (double frequency, double rate)
        {
            double magnitude = 1.0;
            for (size_t band = 0; band < active.size(); ++band)
                magnitude *= ToneStack::makeEQCoefficients (band, active[band], rate)
                    ->getMagnitudeForFrequency (frequency, rate);
            return magnitude;
        };

        for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0 })
        {
            if (preset == ToneStack::EQPreset::flat)
                for (double frequency : { 20.0, 1000.0, rate * 0.45 })
                    check (std::abs (magnitudeAt (frequency, rate) - 1.0) < 0.001,
                           "reset preset is a flat response");
            if (preset == ToneStack::EQPreset::highPass || preset == ToneStack::EQPreset::bandPass)
                check (magnitudeAt (20.0, rate) < 0.02, "high-pass/band-pass removes low frequencies");
            if (preset == ToneStack::EQPreset::lowPass || preset == ToneStack::EQPreset::bandPass)
                check (magnitudeAt (rate * 0.45, rate) < 0.03, "low-pass/band-pass removes high frequencies");
            if (preset == ToneStack::EQPreset::bandPass)
                check (magnitudeAt (1000.0, rate) > 0.9, "band-pass preserves its pass band");

            for (size_t band = 0; band < active.size(); ++band)
                if (active[band].type == ToneStack::EQType::lowPass
                    || active[band].type == ToneStack::EQType::highPass)
                {
                    const double gain = juce::Decibels::gainToDecibels (
                        ToneStack::makeEQCoefficients (band, active[band], rate)
                            ->getMagnitudeForFrequency (active[band].frequency, rate));
                    check (std::abs (gain + 3.0103) < 0.01, "cutoff marker matches the -3 dB response");
                }
        }

        const double frequency = preset == ToneStack::EQPreset::lowPass ? 4000.0
            : preset == ToneStack::EQPreset::flat ? 1000.0 : 100.0;
        constexpr double rate = 48000.0;
        for (int channels : { 1, 2 })
        {
            ToneStack tone;
            tone.prepare ({ rate, 512, (juce::uint32) channels });
            tone.setEQ (active);
            tone.setFilter (ToneStack::FilterType::lowPass, 20000.0f, 0.707f, 1.0f, 0.0f, 0.0f);
            tone.setReverb (0.0f);
            juce::AudioBuffer<float> audio (channels, 512);
            std::array<double, 3> inputPower {}, outputPower {};
            for (int block = 0; block < 360; ++block)
            {
                if (block == 120)
                    tone.setEQEnabled (false);
                if (block == 240)
                    tone.setEQEnabled (true);
                const int stage = block / 120;
                const bool measuring = block % 120 >= 80;
                for (int sample = 0; sample < 512; ++sample)
                {
                    const float input = (float) (0.05 * std::sin (
                        2.0 * juce::MathConstants<double>::pi * frequency * (block * 512 + sample) / rate));
                    for (int channel = 0; channel < channels; ++channel)
                        audio.setSample (channel, sample, input);
                    if (measuring)
                        inputPower[(size_t) stage] += (double) input * input;
                }
                tone.process (audio);
                if (measuring)
                    for (int sample = 0; sample < 512; ++sample)
                    {
                        const float output = audio.getSample (0, sample);
                        check (std::isfinite (output), "EQ bypass transitions remain finite");
                        outputPower[(size_t) stage] += (double) output * output;
                        if (stage == 1)
                        {
                            const float input = (float) (0.05 * std::sin (
                                2.0 * juce::MathConstants<double>::pi * frequency * (block * 512 + sample) / rate));
                            for (int channel = 0; channel < channels; ++channel)
                                check (std::abs (audio.getSample (channel, sample) - input) < 0.000001f,
                                       "bypassed EQ returns unchanged dry audio");
                        }
                    }
            }
            for (size_t stage = 0; stage < 3; ++stage)
            {
                const double expected = stage == 1 ? 1.0 : magnitudeAt (frequency, rate);
                const double measured = std::sqrt (outputPower[stage] / inputPower[stage]);
                check (std::abs (20.0 * std::log10 (measured / expected)) < 0.08,
                       "preset response and bypass/re-enable match the displayed coefficients");
            }
        }
    }

    for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0 })
        for (int channels : { 1, 2 })
            for (auto type : { ToneStack::FilterType::lowPass, ToneStack::FilterType::highPass,
                               ToneStack::FilterType::bandPass, ToneStack::FilterType::notch })
            {
                ToneStack tone;
                tone.prepare ({ rate, 512, (juce::uint32) channels });
                tone.setEQ (ToneStack::makeEQPreset (ToneStack::EQPreset::flat));
                tone.setEQEnabled (false);
                tone.setReverb (0.0f);
                juce::AudioBuffer<float> audio (channels, 512), dry (channels, 512);
                for (const auto position : {
                    ToneStack::FilterValues { type, ToneStack::filterPadCentreCutoff, ToneStack::filterPadCentreResonance },
                    ToneStack::FilterValues { type, 600.0f, ToneStack::filterPadCentreResonance },
                    ToneStack::FilterValues { type, ToneStack::filterPadCentreCutoff, 1.2f },
                    ToneStack::FilterValues { type, 600.0f, 1.2f }, ToneStack::FilterValues { type, 1800.0f, 1.2f },
                    ToneStack::FilterValues { type, 600.0f, 4.0f }, ToneStack::FilterValues { type, 1800.0f, 4.0f },
                    ToneStack::FilterValues { type, ToneStack::filterPadCentreCutoff, ToneStack::filterPadCentreResonance } })
                {
                    tone.setFilter (type, position.cutoff, position.resonance, 6.0f, 1.0f, 0.0f);
                    float difference = 0.0f;
                    for (int block = 0; block < 8; ++block)
                    {
                        for (int sample = 0; sample < 512; ++sample)
                            for (int channel = 0; channel < channels; ++channel)
                                audio.setSample (channel, sample, (float) (0.35 * std::sin (
                                    2.0 * juce::MathConstants<double>::pi * 7000.0 * (block * 512 + sample) / rate)));
                        dry.makeCopyOf (audio);
                        tone.process (audio);
                        for (int channel = 0; channel < channels; ++channel)
                            for (int sample = 0; sample < 512; ++sample)
                            {
                                check (std::isfinite (audio.getSample (channel, sample)),
                                       "filter pad bypass transitions remain finite");
                                if (block == 7)
                                    difference = juce::jmax (difference, std::abs (
                                        audio.getSample (channel, sample) - dry.getSample (channel, sample)));
                            }
                    }
                    check (ToneStack::isFilterPadCentred (position.cutoff, position.resonance)
                            ? difference < 0.000001f : difference > 0.0001f,
                           "center bypasses every filter shape and drive; axes and quadrants reactivate filtering");
                }
            }

    for (int modulationMode : { 0, 1, 2 })
    {
        ToneStack tone, reference;
        for (auto* effects : { &tone, &reference })
        {
            effects->prepare ({ 48000.0, 512, 2 });
            auto activeEQ = ToneStack::makeEQPreset (ToneStack::EQPreset::flat);
            activeEQ[3].gainDb = 6.0f;
            effects->setEQ (activeEQ);
            effects->setFilter (ToneStack::FilterType::lowPass, ToneStack::filterPadCentreCutoff,
                                ToneStack::filterPadCentreResonance, 6.0f, 1.0f, 0.0f);
            effects->setReverb (0.3f);
            effects->snapParametersToTargets();
        }
        reference.setFilterEnabled (false);
        reference.snapParametersToTargets();
        if (modulationMode == 1)
        {
            tone.setFilterMotion (ToneStack::FilterMotion::lowPassSweep);
            tone.setFilterPlayback (0.25, true, true);
        }
        if (modulationMode == 2)
            tone.setFilter (ToneStack::FilterType::lowPass, ToneStack::filterPadCentreCutoff,
                            ToneStack::filterPadCentreResonance, 6.0f, 1.0f, 1.0f);
        juce::AudioBuffer<float> audio (2, 512), expected (2, 512);
        float difference = 0.0f;
        for (int block = 0; block < 12; ++block)
        {
            for (int sample = 0; sample < 512; ++sample)
                for (int channel = 0; channel < 2; ++channel)
                    audio.setSample (channel, sample, (float) (0.1 * std::sin (
                        2.0 * juce::MathConstants<double>::pi * 1000.0 * (block * 512 + sample) / 48000.0)));
            expected.makeCopyOf (audio);
            tone.process (audio);
            reference.process (expected);
            if (block == 11)
                for (int channel = 0; channel < 2; ++channel)
                    for (int sample = 0; sample < 512; ++sample)
                        difference = juce::jmax (difference, std::abs (
                            audio.getSample (channel, sample) - expected.getSample (channel, sample)));
        }
        check (modulationMode != 0 ? difference > 0.0001f : difference < 0.000001f,
               "center bypass preserves EQ/reverb and explicit motion/mod wheel still activates filtering");
    }

    for (auto motion : { ToneStack::FilterMotion::lowPassSweep, ToneStack::FilterMotion::highPassSweep,
                         ToneStack::FilterMotion::bandPassSweep, ToneStack::FilterMotion::resonanceModulation })
    {
        for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0 })
        {
            const ToneStack::FilterValues manual { ToneStack::FilterType::lowPass, 1500.0f, 0.707f };
            for (double phase : { 0.0, 0.125, 0.25, 0.5, 0.75, 0.999 })
            {
                const auto values = ToneStack::evaluateFilterMotion (motion, phase, manual, rate);
                const auto repeated = ToneStack::evaluateFilterMotion (motion, phase + 1.0, manual, rate);
                check (std::abs (values.cutoff - repeated.cutoff) < 0.01f
                        && std::abs (values.resonance - repeated.resonance) < 0.00001f,
                       "filter motion repeats exactly every cycle");
                check (values.cutoff >= 20.0f && values.cutoff <= juce::jmin (20000.0f, (float) rate * 0.49f) + 0.01f,
                       "automated cutoff stays inside safe frequency limits");
                if (motion == ToneStack::FilterMotion::lowPassSweep)
                    check (values.type == ToneStack::FilterType::lowPass, "low-pass sweep uses a low-pass filter");
                if (motion == ToneStack::FilterMotion::highPassSweep)
                    check (values.type == ToneStack::FilterType::highPass, "high-pass sweep uses a high-pass filter");
                if (motion == ToneStack::FilterMotion::bandPassSweep)
                    check (values.type == ToneStack::FilterType::bandPass, "band-pass sweep uses a band-pass filter");
                if (motion == ToneStack::FilterMotion::resonanceModulation)
                {
                    check (values.cutoff == manual.cutoff, "resonance motion leaves cutoff unchanged");
                    check (values.resonance >= 0.707f && values.resonance <= 3.5f,
                           "resonance modulation stays within its specified range");
                }
            }
            const auto minimum = ToneStack::evaluateFilterMotion (motion, 0.0, manual, rate);
            const auto maximum = ToneStack::evaluateFilterMotion (motion, 0.5, manual, rate);
            if (motion != ToneStack::FilterMotion::resonanceModulation)
                check (std::abs (minimum.cutoff - 20.0f) < 0.01f && maximum.cutoff > minimum.cutoff * 100.0f,
                       "sweeps traverse the logarithmic frequency range");
            else
                check (std::abs (minimum.resonance - 0.707f) < 0.0001f
                        && std::abs (maximum.resonance - 3.5f) < 0.0001f,
                       "resonance motion reaches both ends of its range");
        }

        for (int channels : { 1, 2 })
        {
            constexpr double rate = 48000.0;
            ToneStack tone;
            tone.prepare ({ rate, 512, (juce::uint32) channels });
            auto activeEQ = settings;
            activeEQ[3].gainDb = 6.0f;
            tone.setEQ (activeEQ);
            tone.setFilter (ToneStack::FilterType::lowPass, 1500.0f, 0.707f, 1.0f, 1.0f, 0.0f);
            tone.setFilterMotion (motion);
            tone.setFilterPlayback (0.25, true, true);
            tone.setReverb (0.0f);
            juce::AudioBuffer<float> audio;
            auto renderSilence = [&] (int samples)
            {
                while (samples > 0)
                {
                    const int block = juce::jmin (512, samples);
                    audio.setSize (channels, block);
                    audio.clear();
                    tone.process (audio);
                    check (std::isfinite (tone.getLiveFilterCutoff()) && std::isfinite (tone.getLiveFilterResonance()),
                           "live automation values stay finite");
                    samples -= block;
                }
            };
            renderSilence (3000);
            check (std::abs (tone.getFilterMotionPhase() - 0.25) < 0.00000001,
                   "automation phase matches loaded sample duration");
            renderSilence (24000);
            check (std::abs (tone.getFilterMotionPhase() - 0.25) < 0.00000001,
                   "automation repeats across multiple sample durations");
            tone.setFilterPlayback (0.25, false, false);
            renderSilence (3000);
            check (std::abs (tone.getFilterMotionPhase() - 0.25) < 0.00000001,
                   "automation pauses when playback stops");
            tone.setFilterPlayback (0.25, true, false);
            tone.setFilterEnabled (false);
            renderSilence (3000);
            check (std::abs (tone.getFilterMotionPhase() - 0.25) < 0.00000001,
                   "automation stops advancing when the filter is off");

            const double frequency = activeEQ[3].frequency;
            double expected = 1.0;
            for (size_t band = 0; band < activeEQ.size(); ++band)
                expected *= ToneStack::makeEQCoefficients (band, activeEQ[band], rate)
                    ->getMagnitudeForFrequency (frequency, rate);
            double inputPower = 0.0, outputPower = 0.0;
            for (int block = 0; block < 180; ++block)
            {
                audio.setSize (channels, 512);
                for (int sample = 0; sample < 512; ++sample)
                {
                    const float input = (float) (0.05 * std::sin (
                        2.0 * juce::MathConstants<double>::pi * frequency * (block * 512 + sample) / rate));
                    for (int channel = 0; channel < channels; ++channel)
                        audio.setSample (channel, sample, input);
                    if (block >= 100)
                        inputPower += (double) input * input;
                }
                tone.process (audio);
                if (block >= 100)
                    for (int sample = 0; sample < 512; ++sample)
                    {
                        const float output = audio.getSample (0, sample);
                        outputPower += (double) output * output;
                    }
            }
            check (std::abs (20.0 * std::log10 (std::sqrt (outputPower / inputPower) / expected)) < 0.08,
                   "filter bypass leaves the independent EQ response intact");

            tone.setFilterEnabled (true);
            tone.setFilterPlayback (0.5, true, true);
            check (tone.getFilterMotionPhase() == 0.0, "new notes restart filter automation");
            renderSilence (6000);
            check (std::abs (tone.getFilterMotionPhase() - 0.25) < 0.00000001,
                   "cycle timing recalibrates for a changed playback duration");
            tone.setFilterMotion (ToneStack::FilterMotion::none);
            tone.setFilter (ToneStack::FilterType::lowPass, 1500.0f, 0.707f, 1.0f, 1.0f, 0.0f);
            renderSilence (3000);
            check (tone.getFilterMotionPhase() == 0.0 && std::abs (tone.getLiveFilterCutoff() - 1500.0f) < 0.1f,
                   "deselecting motion restores the manual filter without advancing automation");
        }
    }

    std::cout << "EQ/filter checks: " << (failures == 0 ? "PASS" : "FAIL") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 3;
}

static int runLoopChecks()
{
    using namespace drumbreaker;
    int failures = 0;
    auto check = [&failures] (bool passed, const char* description)
    {
        if (! passed)
        {
            std::cerr << "FAILED: " << description << "\n";
            ++failures;
        }
    };
    auto audio = std::make_shared<juce::AudioBuffer<float>> (1, 1024);
    for (int sample = 0; sample < 1024; ++sample)
        audio->setSample (0, sample, (float) ((sample < 512 ? 0.2 : 0.4)
            * std::sin (2.0 * juce::MathConstants<double>::pi * sample / 64.0)));
    BreakData::Ptr data = new BreakData();
    data->audio = audio;
    data->sourceSampleRate = 48000.0;
    data->slices = { { 0, 512 }, { 512, 1024 } };
    data->arrangement = { 1, 0, 1 };
    data->rebuildTimeline();
    const int length = (int) data->totalLength;
    BreakEngine engine;
    engine.prepare (48000.0, 512);
    engine.setData (data);
    engine.noteOn (60, 1.0f);
    juce::AudioBuffer<float> repeated (2, length * 3);
    repeated.clear();
    engine.renderNextBlock (repeated, 0, repeated.getNumSamples());
    check (engine.isPlaying(), "held note remains active after three full arrangement cycles");
    check (engine.getPlayheadNormalised() == 0.0f, "playhead returns to start at an exact loop boundary");
    double maximumError = 0.0;
    for (int sample = 0; sample < length; ++sample)
        for (int cycle = 1; cycle < 3; ++cycle)
            maximumError = juce::jmax (maximumError,
                (double) std::abs (repeated.getSample (0, sample) - repeated.getSample (0, sample + cycle * length)));
    check (maximumError < 0.000001 && repeated.getMagnitude (0, length) > 0.01f,
           "every cycle reproduces the complete reordered/duplicated arrangement without inserted gaps");

    {
        BreakEngine loopingEngine;
        loopingEngine.prepare (48000.0, 257);
        loopingEngine.setData (data);
        loopingEngine.setPitchOffsetSemitones (7.25f);
        loopingEngine.noteOn (60, 1.0f);
        ToneStack effects;
        effects.prepare ({ 48000.0, 257, 2 });
        effects.setEQ (ToneStack::makeEQPreset (ToneStack::EQPreset::flat));
        effects.setFilter (ToneStack::FilterType::lowPass, 1500.0f, 0.707f, 1.0f, 1.0f, 0.0f);
        effects.setFilterMotion (ToneStack::FilterMotion::lowPassSweep);
        juce::AudioBuffer<float> block (2, 257);
        for (int pass = 0; pass < 60; ++pass)
        {
            block.clear();
            loopingEngine.renderNextBlock (block, 0, 257);
            const double duration = (double) length / data->sourceSampleRate / loopingEngine.getPlaybackPitchRatio();
            effects.setFilterPlayback (duration, loopingEngine.isPlaying(), pass == 0);
            effects.process (block);
            const double difference = std::abs (effects.getFilterMotionPhase() - loopingEngine.getPlayheadNormalised());
            check (juce::jmin (difference, 1.0 - difference) < 0.00001,
                   "sample-timed filter motion remains aligned across pitched playback loops");
        }
    }

    for (double hostRate : { 44100.0, 48000.0 })
        for (float pitch : { -12.0f, 7.25f, 12.0f, 24.0f })
        {
            engine.prepare (hostRate, 512);
            engine.setData (data);
            engine.setPitchOffsetSemitones (pitch);
            engine.noteOn (60, 1.0f);
            juce::AudioBuffer<float> block (2, 257);
            double sourcePosition = 0.0;
            for (int pass = 0; pass < 30; ++pass)
            {
                block.clear();
                engine.renderNextBlock (block, 0, 257);
                sourcePosition = std::fmod (sourcePosition + 257.0 * data->sourceSampleRate / hostRate
                    * std::pow (2.0, pitch / 12.0), (double) length);
                check (engine.isPlaying() && std::abs (engine.getPlayheadNormalised()
                        - sourcePosition / length) < 0.00001, "pitched/rate-converted looping preserves fractional overshoot");
                check (block.getMagnitude (0, 257) > 0.0001f, "pitched looping continues producing audio across wrap boundaries");
            }
            engine.noteOff (60);
            block.setSize (2, 2048);
            block.clear();
            engine.renderNextBlock (block, 0, 2048);
            check (! engine.isPlaying(), "release stops pitched looping within the fixed release time");
            block.clear();
            engine.renderNextBlock (block, 0, 2048);
            check (block.getMagnitude (0, 2048) == 0.0f, "released loop does not restart later");
        }

    engine.prepare (48000.0, 512);
    engine.setPitchOffsetSemitones (0.0f);
    engine.noteOn (60, 1.0f);
    engine.noteOn (72, 1.0f);
    engine.noteOff (72);
    juce::AudioBuffer<float> fallback (2, length * 3);
    fallback.clear();
    engine.renderNextBlock (fallback, 0, fallback.getNumSamples());
    check (engine.isPlaying() && std::abs (engine.getPlaybackPitchRatio() - 1.0) < 0.00001,
           "releasing the latest note returns to the earlier held note's looping pitch");
    engine.allNotesOff();
    fallback.clear();
    engine.renderNextBlock (fallback, 0, fallback.getNumSamples());
    check (! engine.isPlaying(), "all-notes-off stops held looping");

    engine.prepare (48000.0, 512);
    engine.setLooping (false);
    engine.noteOn (60, 1.0f);
    repeated.clear();
    engine.renderNextBlock (repeated, 0, repeated.getNumSamples());
    check (! engine.isPlaying() && repeated.getMagnitude (length, length * 2) == 0.0f,
           "explicit one-shot mode never repeats into the export effect tail");
    std::cout << "Loop playback checks: " << (failures == 0 ? "PASS" : "FAIL") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 8;
}

static int runExportChecks()
{
    using namespace drumbreaker;
    licenseTest::FixtureStore licenseStore;
    licensing::LicenseClient exportLicense ("fixture-device", licenseStore.options());
    if (! exportLicense.activate (licenseTest::key) || ! licenseTest::waitForIdle (exportLicense)
        || ! exportLicense.isVerified())
    {
        std::cerr << "FAILED: synthetic export license could not be verified\n";
        return 5;
    }
    int failures = 0;
    auto check = [&failures] (bool result, const char* name)
    {
        if (! result)
        {
            std::cerr << "FAILED: " << name << "\n";
            ++failures;
        }
    };
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("DrumBreaker-export-test", "", false);
    if (directory.createDirectory().failed())
        return 5;
    struct Cleanup
    {
        juce::File directory;
        ~Cleanup() { directory.deleteRecursively(); }
    } cleanup { directory };

    constexpr double rate = 48000.0;
    auto source = std::make_shared<juce::AudioBuffer<float>> (1, 4800);
    for (int sample = 0; sample < source->getNumSamples(); ++sample)
        source->setSample (0, sample, (float) ((sample < 2400 ? 0.2 : 0.4)
            * std::sin (2.0 * juce::MathConstants<double>::pi * (sample < 2400 ? 480.0 : 960.0) * sample / rate)));
    BreakData::Ptr data = new BreakData();
    data->audio = source;
    data->sourceSampleRate = rate;
    data->name = "test.wav";
    data->slices = { { 0, 2400 }, { 2400, 4800 } };
    data->arrangement = { 1, 0, 1 };
    data->rebuildTimeline();
    OfflineExporter::Snapshot snapshot;
    snapshot.license = exportLicense.getAccess();
    snapshot.data = data;
    snapshot.sampleRate = rate;
    snapshot.channels = 2;
    snapshot.eq = ToneStack::makeEQPreset (ToneStack::EQPreset::flat);
    snapshot.eqEnabled = false;
    snapshot.filterEnabled = false;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

        const auto lockedFile = directory.getChildFile ("unlicensed.wav");
        auto locked = snapshot;
        locked.license = {};
        check (OfflineExporter::writeWav (locked, lockedFile).failed() && ! lockedFile.exists(),
            "direct unlicensed renderer calls fail without creating a WAV");

    const auto dryFile = directory.getChildFile ("dry.wav");
    check (OfflineExporter::writeWav (snapshot, dryFile).wasOk(), "dry export succeeds");
    std::unique_ptr<juce::AudioFormatReader> dry (formats.createReaderFor (dryFile));
    check (dry != nullptr, "exported WAV opens");
    if (dry == nullptr)
        return 5;
    check (dry->usesFloatingPointData && dry->bitsPerSample == 32, "export is 32-bit float WAV");
    check (dry->numChannels == 2 && dry->sampleRate == rate && dry->lengthInSamples == 7200,
           "output channels, rate and repeated/reordered arrangement length match");
    juce::AudioBuffer<float> actual (2, 7200), expected (2, 7200);
    actual.clear();
    expected.clear();
    dry->read (&actual, 0, 7200, 0, true, true);
    BreakEngine reference;
    reference.prepare (rate, 512);
    reference.setData (data->withArrangement (data->arrangement));
    reference.noteOn (60, 1.0f);
    reference.renderNextBlock (expected, 0, 7200);
    float maximumError = 0.0f;
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 7200; ++sample)
            maximumError = juce::jmax (maximumError, std::abs (actual.getSample (channel, sample) - expected.getSample (channel, sample)));
    check (maximumError < 0.000001f, "export matches arranged C5 playback sample-for-sample");
    check (data->totalLength == 7200 && data->arrangement == std::vector<int> { 1, 0, 1 },
           "export leaves the original arrangement unchanged");
    check (OfflineExporter::writeWav (snapshot, dryFile).failed(), "existing WAVs cannot be silently overwritten");

    auto centred = snapshot;
    centred.filterEnabled = true;
    centred.filter.cutoff = ToneStack::filterPadCentreCutoff;
    centred.filter.resonance = ToneStack::filterPadCentreResonance;
    centred.filterDrive = 6.0f;
    const auto centredFile = directory.getChildFile ("centred.wav");
    check (OfflineExporter::writeWav (centred, centredFile).wasOk(), "centered filter export succeeds");
    std::unique_ptr<juce::AudioFormatReader> centredReader (formats.createReaderFor (centredFile));
    check (centredReader != nullptr && centredReader->lengthInSamples == 7200,
           "centered filter export has no filter decay tail");
    if (centredReader != nullptr && centredReader->lengthInSamples == 7200)
    {
        juce::AudioBuffer<float> centredAudio (2, 7200);
        centredReader->read (&centredAudio, 0, 7200, 0, true, true);
        float centredError = 0.0f;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 7200; ++sample)
                centredError = juce::jmax (centredError, std::abs (
                    centredAudio.getSample (channel, sample) - actual.getSample (channel, sample)));
        check (centredError < 0.000001f, "centered export matches dry playback even at maximum drive");
    }

    const auto pitchedFile = directory.getChildFile ("pitched.wav");
    snapshot.pitchSemitones = 12.0f;
    check (OfflineExporter::writeWav (snapshot, pitchedFile).wasOk(), "pitched export succeeds");
    std::unique_ptr<juce::AudioFormatReader> pitched (formats.createReaderFor (pitchedFile));
    check (pitched != nullptr && pitched->lengthInSamples == 3600, "pitch doubles playback rate and halves exported duration");

    const auto wetFile = directory.getChildFile ("wet.wav");
    snapshot.pitchSemitones = 0.0f;
    snapshot.eqEnabled = true;
    snapshot.eq[3].gainDb = 6.0f;
    snapshot.filterEnabled = true;
    snapshot.filter.cutoff = 1000.0f;
    snapshot.filter.resonance = 1.2f;
    snapshot.filterDrive = 1.6f;
    snapshot.filterMix = 0.7f;
    snapshot.motion = ToneStack::FilterMotion::lowPassSweep;
    snapshot.reverb = 0.3f;
    snapshot.outputDb = -3.0f;
    check (OfflineExporter::writeWav (snapshot, wetFile).wasOk(), "processed export succeeds");
    std::unique_ptr<juce::AudioFormatReader> wet (formats.createReaderFor (wetFile));
    check (wet != nullptr && wet->lengthInSamples == 7200 + 192000, "reverb export includes its four-second tail");
    if (wet != nullptr)
    {
        juce::AudioBuffer<float> rendered (2, (int) wet->lengthInSamples);
        rendered.clear();
        wet->read (&rendered, 0, rendered.getNumSamples(), 0, true, true);
        check (rendered.getMagnitude (7200, 20000) > 0.00001f, "reverb tail contains audible effect output");
        float difference = 0.0f;
        for (int sample = 0; sample < 7200; ++sample)
            difference = juce::jmax (difference, std::abs (rendered.getSample (0, sample) - actual.getSample (0, sample)));
        check (difference > 0.01f, "EQ/filter motion/reverb/output processing changes the exported audio");
    }

    {
        auto bandPass = snapshot;
        bandPass.motion = ToneStack::FilterMotion::bandPassSweep;
        bandPass.eqEnabled = false;
        bandPass.reverb = 0.0f;
        const auto bandPassFile = directory.getChildFile ("band-pass.wav");
        check (OfflineExporter::writeWav (bandPass, bandPassFile).wasOk(),
               "automated band-pass export succeeds");
        std::unique_ptr<juce::AudioFormatReader> bandPassReader (formats.createReaderFor (bandPassFile));
        const auto tailFrames = (juce::int64) std::ceil (rate * juce::jlimit (0.02, 4.0,
            0.02 + 12.0 * bandPass.filter.resonance / (juce::MathConstants<double>::pi * 20.0)));
        check (bandPassReader != nullptr && bandPassReader->lengthInSamples == 7200 + tailFrames,
               "band-pass export uses the swept minimum cutoff for its complete decay tail");
    }

        snapshot.eqEnabled = false;
        snapshot.motion = ToneStack::FilterMotion::none;
        snapshot.filter.cutoff = 100.0f;
        snapshot.filter.resonance = 4.0f;
        snapshot.filterMix = 1.0f;
        snapshot.reverb = 0.0f;
        const auto ringingFile = directory.getChildFile ("filter-tail.wav");
        check (OfflineExporter::writeWav (snapshot, ringingFile).wasOk(), "resonant filter export succeeds without reverb");
        std::unique_ptr<juce::AudioFormatReader> ringing (formats.createReaderFor (ringingFile));
        check (ringing != nullptr && ringing->lengthInSamples > 7200,
            "resonant filter decay is not truncated at the end of the arrangement");
        if (ringing != nullptr && ringing->lengthInSamples > 7200)
        {
         juce::AudioBuffer<float> decay (2, (int) ringing->lengthInSamples - 7200);
         decay.clear();
         ringing->read (&decay, 0, decay.getNumSamples(), 7200, true, true);
         check (decay.getMagnitude (0, decay.getNumSamples()) > 0.000001f,
             "filter tail retains post-sample ringing");
        }

        snapshot = OfflineExporter::Snapshot();
    snapshot.license = exportLicense.getAccess();
    snapshot.data = data;
    snapshot.sampleRate = rate;
    snapshot.eqEnabled = false;
    snapshot.filterEnabled = false;
    snapshot.outputDb = 12.0f;
    const auto boostedFile = directory.getChildFile ("boosted.wav");
    check (OfflineExporter::writeWav (snapshot, boostedFile).wasOk(), "boosted export succeeds");
    std::unique_ptr<juce::AudioFormatReader> boosted (formats.createReaderFor (boostedFile));
    if (boosted != nullptr)
    {
        juce::AudioBuffer<float> rendered (2, 7200);
        rendered.clear();
        boosted->read (&rendered, 0, 7200, 0, true, true);
        check (rendered.getMagnitude (0, 7200) > 1.0f, "float WAV preserves boosted peaks without normalization or PCM clipping");
    }
    else
        check (false, "boosted WAV opens");

    int calls = 0;
    const auto cancelledFile = directory.getChildFile ("cancelled.wav");
    check (OfflineExporter::writeWav (snapshot, cancelledFile, [&calls] { return ++calls > 4; }).failed()
            && ! cancelledFile.existsAsFile(), "cancellation does not publish a partial WAV");
    snapshot.data = nullptr;
    const auto emptyFile = directory.getChildFile ("empty.wav");
    check (OfflineExporter::writeWav (snapshot, emptyFile).failed() && ! emptyFile.existsAsFile(),
           "empty export fails without creating a file");
    snapshot.data = data;
    const auto lostLicenseFile = directory.getChildFile ("lost-license.wav");
    int authorizationChecks = 0;
    const auto lostLicense = OfflineExporter::writeWav (snapshot, lostLicenseFile, [&]
    {
        if (++authorizationChecks == 4)
            exportLicense.activate ("wrong-fixture-key");
        return false;
    });
    check (lostLicense.failed() && ! lostLicenseFile.exists(),
           "authorization lost during rendering prevents publishing the unfinished WAV");
    check (licenseTest::waitForIdle (exportLicense) && ! exportLicense.isVerified(),
           "rejected key leaves renderer access revoked");
    dry.reset();
    pitched.reset();
    wet.reset();
    boosted.reset();
    ringing.reset();
    std::cout << "Export checks: " << (failures == 0 ? "PASS" : "FAIL") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 5;
}

static int runTempoChecks()
{
    int failures = 0;
    for (double rate : { 44100.0, 48000.0 })
        for (double tempo : { 167.0, 170.0, 167.375 })
            for (bool syncopated : { false, true })
            {
                const double samplesPerBeat = rate * 60.0 / tempo;
                juce::AudioBuffer<float> audio (1, (int) std::round (samplesPerBeat * 16.0));
                audio.clear();
                const double spacing = syncopated ? 0.75 : 1.0;
                for (double beat = 0.0; beat < 16.0; beat += spacing)
                {
                    const int start = (int) std::round (beat * samplesPerBeat);
                    for (int sample = 0; sample < (int) (rate * 0.025) && start + sample < audio.getNumSamples(); ++sample)
                    {
                        const float value = (float) (0.8 * std::exp (-sample / (rate * 0.005))
                            * std::cos (2.0 * juce::MathConstants<double>::pi * 120.0 * sample / rate));
                        audio.addSample (0, start + sample, value);
                    }
                }
                const double measured = drumbreaker::SampleAnalyzer::detectTempo (audio, rate);
                std::cout << "tempo=" << tempo << " rate=" << rate << " syncopated=" << syncopated
                          << " measured=" << measured << "\n";
                if (std::abs (measured - tempo) > 0.001)
                    ++failures;

                if (! syncopated)
                {
                    const int originalLength = audio.getNumSamples();
                    audio.setSize (1, originalLength + (int) std::round (rate * 0.15), true, true);
                    const double padded = drumbreaker::SampleAnalyzer::detectTempo (audio, rate);
                    std::cout << "tempo=" << tempo << " rate=" << rate << " tail=0.15 measured=" << padded << "\n";
                    if (std::abs (padded - tempo) > 0.15)
                        ++failures;
                }
            }
    juce::AudioBuffer<float> silent (2, 44100);
    silent.clear();
    if (drumbreaker::SampleAnalyzer::detectTempo (silent, 44100.0) != 0.0)
        ++failures;
    std::cout << "Tempo checks: " << (failures == 0 ? "PASS" : "FAIL") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 4;
}

int main (int argc, char** argv)
{
    if (argc == 2 && juce::String (argv[1]) == "--loop-self-test")
        return runLoopChecks();

    if (argc == 2 && juce::String (argv[1]) == "--license-persistence-self-test")
        return runLicensePersistenceChecks();

    if (argc == 2 && juce::String (argv[1]) == "--license-policy-self-test")
        return runLicensePolicyChecks();

    if (argc == 2 && juce::String (argv[1]) == "--export-self-test")
        return runExportChecks();

    if (argc == 2 && juce::String (argv[1]) == "--tempo-self-test")
        return runTempoChecks();

    if (argc == 2 && juce::String (argv[1]) == "--eq-self-test")
        return runEQChecks();

    if (argc < 2)
    {
        std::cout << "usage: AnalyzerTest <audio file> [expected BPM] | --eq-self-test | --tempo-self-test | --export-self-test\n";
        return 1;
    }

    const juce::File file { juce::String (argv[1]) };
    std::cout << "file: " << file.getFullPathName() << "  exists=" << (int) file.existsAsFile() << "\n";

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::cout << "registered formats:";
    for (auto* f : formats)
        std::cout << " " << f->getFormatName();
    std::cout << "\n";

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        std::cout << "FAILED: no reader for this file\n";
        return 2;
    }

    std::cout << "channels=" << reader->numChannels
              << " sampleRate=" << reader->sampleRate
              << " length=" << reader->lengthInSamples
              << " bits=" << reader->bitsPerSample
              << " float=" << (int) reader->usesFloatingPointData
              << " seconds=" << (double) reader->lengthInSamples / reader->sampleRate << "\n";

    juce::AudioBuffer<float> audio ((int) juce::jlimit<juce::int64> (1, 2, reader->numChannels),
                                    (int) reader->lengthInSamples);
    audio.clear();
    reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);

    std::cout << "peak (AudioBuffer overload)=" << audio.getMagnitude (0, audio.getNumSamples()) << "\n";

    juce::AudioBuffer<float> audio2 ((int) reader->numChannels, (int) reader->lengthInSamples);
    audio2.clear();
    reader->read (audio2.getArrayOfWritePointers(), audio2.getNumChannels(),
                  0, audio2.getNumSamples());

    std::cout << "peak (raw pointers)=" << audio2.getMagnitude (0, audio2.getNumSamples()) << "\n";

    const double bpm = drumbreaker::SampleAnalyzer::detectTempo (audio, reader->sampleRate);
    std::cout << std::fixed << std::setprecision (6) << "bpm=" << bpm << "\n";
    if (argc >= 3)
    {
        const double expected = juce::String (argv[2]).getDoubleValue();
        if (expected <= 0.0 || std::abs (bpm - expected) > 0.01)
        {
            std::cout << "FAILED: expected BPM " << expected << " within 0.01\n";
            return 4;
        }
        std::cout << "reference tempo check: PASS\n";
    }

    drumbreaker::SampleAnalyzer::Settings settings;
    const auto slices = drumbreaker::SampleAnalyzer::detectSlices (audio, reader->sampleRate, settings);
    std::cout << "slices=" << slices.size() << "\n";

    for (size_t i = 0; i < juce::jmin<size_t> (12, slices.size()); ++i)
        std::cout << "  [" << i << "] " << slices[i].start << " .. " << slices[i].end << "\n";

    return 0;
}
