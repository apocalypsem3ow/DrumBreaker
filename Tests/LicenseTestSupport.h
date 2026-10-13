#pragma once

#include "Licensing/LicenseClient.h"

namespace licenseTest
{

inline constexpr const char* key = "fixture-only-not-a-customer-key";

inline drumbreaker::licensing::LicenseResponse approvedReply (const drumbreaker::licensing::LicenseRequest& request)
{
    using namespace drumbreaker::licensing;
    auto reply = juce::JSON::parse (R"({
        "activated":true,"valid":true,"error":null,
        "license_key":{"status":"active","key":"fixture-only-not-a-customer-key","expires_at":null},
        "instance":{"id":"fixture-instance","name":"fixture-device"},
        "meta":{"product_id":1412490,"variant_id":2205337}
    })");
    if (request.kind == ReplyKind::preflight)
        reply.getDynamicObject()->setProperty ("instance", juce::var());
    else
        reply.getProperty ("instance", {}).getDynamicObject()->setProperty ("name", request.device);
    return { juce::Result::ok(), 200, juce::JSON::toString (reply) };
}

inline bool waitForIdle (drumbreaker::licensing::LicenseClient& client)
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
}

struct FixtureStore
{
    FixtureStore() : scope ("gate-test-" + juce::Uuid().toString()), store (scope) {}
    ~FixtureStore() { store.remove(); }

    drumbreaker::licensing::LicenseClient::Options options() const
    {
        drumbreaker::licensing::LicenseClient::Options result;
        result.storageScope = scope;
        result.restoreOnStartup = false;
        result.transport = approvedReply;
        return result;
    }

    const juce::String scope;
    drumbreaker::licensing::LicenseStore store;
};

} // namespace licenseTest