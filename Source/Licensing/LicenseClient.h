#pragma once

#include "LicensePolicy.h"
#include "LicenseStore.h"
#include "LicenseAccess.h"
#include <functional>

namespace drumbreaker::licensing
{

struct LicenseRequest
{
    ReplyKind kind = ReplyKind::preflight;
    juce::String key, device, instance;
};

struct LicenseResponse
{
    juce::Result transport = juce::Result::ok();
    int httpStatus = 0;
    juce::String json;
};

using LicenseTransport = std::function<LicenseResponse(const LicenseRequest&)>;

CheckedReply requestActivation (const juce::String& key, const juce::String& device,
                                const juce::String& savedInstance, const LicenseTransport&);
juce::String makeDeviceName();

class LicenseClient
{
public:
    struct Options
    {
        juce::String storageScope = "production";
        bool restoreOnStartup = true;
        LicenseTransport transport;
    };

    struct Status
    {
        bool busy = false;
        bool verified = false;
        bool saved = false;
        juce::String message = "Activate your DrumBreaker license to enable playback and WAV export.";
    };

    explicit LicenseClient (juce::String deviceName = makeDeviceName());
    LicenseClient (juce::String deviceName, Options);
    ~LicenseClient();
    bool activate (const juce::String& key);
    bool validateSaved();
    Status getStatus() const;
    bool isVerified() const noexcept { return authorization->load(); }
    LicenseAccess getAccess() const noexcept { return LicenseAccess (authorization); }

private:
    class Job;
    LicenseResponse post (const LicenseRequest&, const std::function<bool()>& cancelled);
    void complete (const juce::String& key, CheckedReply reply, bool saved = false,
                   const juce::String& storageWarning = {});
    void noSavedActivation();

    const juce::String device;
    const std::shared_ptr<std::atomic<bool>> authorization = std::make_shared<std::atomic<bool>> (false);
    const Options options;
    LicenseStore store;
    const juce::String coordinationName;
    mutable juce::CriticalSection lock;
    Status status;
    juce::String verifiedKey, verifiedInstance;
    juce::ThreadPool worker { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseClient)
};

} // namespace drumbreaker::licensing