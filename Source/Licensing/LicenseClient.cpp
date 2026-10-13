#include "LicenseClient.h"
#include <juce_cryptography/juce_cryptography.h>

namespace drumbreaker::licensing
{

CheckedReply requestActivation (const juce::String& key, const juce::String& device,
                                const juce::String& savedInstance, const LicenseTransport& transport)
{
    if (key.trim().isEmpty() || device.isEmpty() || ! transport)
        return { juce::Result::fail ("Enter a license key before activating."), {} };
    const auto trimmed = key.trim();
    auto request = [&] (ReplyKind kind, const juce::String& instance)
    {
        const auto response = transport ({ kind, trimmed, device, instance });
        if (response.transport.failed())
            return CheckedReply { response.transport, {} };
        return checkReply (response.httpStatus, response.json, kind, trimmed, device, instance);
    };
    if (savedInstance.isNotEmpty())
        return request (ReplyKind::validation, savedInstance);
    const auto preflight = request (ReplyKind::preflight, {});
    if (preflight.result.failed())
        return preflight;
    return request (ReplyKind::activation, {});
}

juce::String makeDeviceName()
{
    const auto identifier = juce::SystemStats::getUniqueDeviceID();
    if (identifier.isEmpty())
        return {};
    const auto material = "Hidden Audio/DrumBreaker/" + identifier;
    return "DrumBreaker-" + juce::SHA256 (material.toRawUTF8(), material.getNumBytesAsUTF8()).toHexString();
}

class LicenseClient::Job : public juce::ThreadPoolJob
{
public:
    Job (LicenseClient& client, juce::String suppliedKey, bool restore)
        : juce::ThreadPoolJob ("DrumBreaker license request"), owner (client),
          key (std::move (suppliedKey)), restoreOnly (restore) {}

    JobStatus runJob() override
    {
        juce::InterProcessLock coordination (owner.coordinationName);
        if (! coordination.enter (10000))
        {
            owner.complete (key, { juce::Result::fail ("Another instance is checking licensing. Try again shortly."), {} });
            return jobHasFinished;
        }
        struct Unlock
        {
            juce::InterProcessLock& coordination;
            ~Unlock() { coordination.exit(); }
        } unlock { coordination };
        StoredActivation stored;
        bool found = false;
        const auto loaded = owner.store.load (stored, found);
        if (loaded.failed())
        {
            owner.complete (key, { juce::Result::fail (loaded.getErrorMessage()
                + " Do not reactivate to work around this; contact Hidden Audio support."), {} });
            return jobHasFinished;
        }
        if (restoreOnly && ! found)
        {
            owner.noSavedActivation();
            return jobHasFinished;
        }
        auto submittedKey = restoreOnly ? stored.key : key;
        juce::String savedInstance;
        if (found && stored.key == submittedKey)
        {
            if (stored.device != owner.device)
            {
                owner.complete (submittedKey, { juce::Result::fail (
                    "The saved activation belongs to a different device. Contact Hidden Audio support."), {} });
                return jobHasFinished;
            }
            savedInstance = stored.instance;
        }
        if (savedInstance.isEmpty())
        {
            const juce::ScopedLock memoryGuard (owner.lock);
            if (owner.verifiedKey == submittedKey)
                savedInstance = owner.verifiedInstance;
        }
        if (restoreOnly && savedInstance.isEmpty())
        {
            owner.complete (submittedKey, { juce::Result::fail ("Saved activation is incomplete; no new activation was requested."), {} });
            return jobHasFinished;
        }
        const auto reply = requestActivation (submittedKey, owner.device, savedInstance,
            [this] (const LicenseRequest& request)
            {
                if (shouldExit())
                    return LicenseResponse { juce::Result::fail ("License request cancelled."), 0, {} };
                return owner.options.transport ? owner.options.transport (request)
                                               : owner.post (request, [this] { return shouldExit(); });
            });
        if (reply.result.wasOk())
        {
            const auto saved = owner.store.save ({ submittedKey, owner.device, reply.instanceId });
            owner.complete (submittedKey, reply, saved.wasOk(), saved.failed() ? saved.getErrorMessage() : juce::String());
        }
        else
            owner.complete (submittedKey, reply, found && stored.key == submittedKey);
        return jobHasFinished;
    }

private:
    LicenseClient& owner;
    const juce::String key;
    const bool restoreOnly;
};

LicenseClient::LicenseClient (juce::String deviceName) : LicenseClient (std::move (deviceName), Options()) {}

LicenseClient::LicenseClient (juce::String deviceName, Options configuration)
    : device (std::move (deviceName)), options (std::move (configuration)), store (options.storageScope),
      coordinationName ("HiddenAudio.DrumBreaker.Licensing." + juce::String (productId)
                        + "." + juce::String (variantId) + "." + options.storageScope)
{
    if (options.restoreOnStartup)
    {
        status.busy = true;
        status.message = "Checking saved activation...";
        worker.addJob (new Job (*this, {}, true), true);
    }
}

LicenseClient::~LicenseClient()
{
    worker.removeAllJobs (true, -1);
    authorization->store (false);
}

bool LicenseClient::activate (const juce::String& key)
{
    const juce::ScopedLock guard (lock);
    if (status.busy)
        return false;
    if (key.trim().isEmpty() || device.isEmpty())
    {
        status.message = device.isEmpty() ? "A stable device identifier is unavailable. Contact Hidden Audio support."
                                         : "Enter a license key first.";
        return false;
    }
    status.busy = true;
    status.verified = false;
    authorization->store (false);
    status.message = "Checking license with Lemon Squeezy...";
    worker.addJob (new Job (*this, key.trim(), false), true);
    return true;
}

LicenseClient::Status LicenseClient::getStatus() const
{
    const juce::ScopedLock guard (lock);
    return status;
}

bool LicenseClient::validateSaved()
{
    const juce::ScopedLock guard (lock);
    if (status.busy)
        return false;
    status.busy = true;
    status.verified = false;
    authorization->store (false);
    status.message = "Checking saved activation...";
    worker.addJob (new Job (*this, {}, true), true);
    return true;
}

void LicenseClient::noSavedActivation()
{
    const juce::ScopedLock guard (lock);
    status = Status();
    authorization->store (false);
}

void LicenseClient::complete (const juce::String& key, CheckedReply reply, bool saved, const juce::String& storageWarning)
{
    const juce::ScopedLock guard (lock);
    status.busy = false;
    status.verified = reply.result.wasOk();
    status.saved = saved;
    authorization->store (status.verified);
    if (reply.result.wasOk())
    {
        verifiedKey = key;
        verifiedInstance = reply.instanceId;
        status.message = saved ? "License verified and securely saved. Playback and WAV export are enabled."
            : "License verified, but secure saving failed. Keep this instance open and contact support before restarting. " + storageWarning;
    }
    else
        status.message = reply.result.getErrorMessage()
            + (saved ? " Saved activation was kept; no replacement activation was requested." : juce::String());
}

LicenseResponse LicenseClient::post (const LicenseRequest& request, const std::function<bool()>& cancelled)
{
    auto fail = [] (const char* message)
    {
        return LicenseResponse { juce::Result::fail (message), 0, {} };
    };
    if (cancelled())
        return fail ("License request cancelled.");
    const double deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
    const auto endpoint = request.kind == ReplyKind::activation ? "activate" : "validate";
    auto url = juce::URL ("https://api.lemonsqueezy.com/v1/licenses/" + juce::String (endpoint))
        .withParameter ("license_key", request.key);
    if (request.kind == ReplyKind::activation)
        url = url.withParameter ("instance_name", request.device);
    else if (request.kind == ReplyKind::validation)
        url = url.withParameter ("instance_id", request.instance);
    int statusCode = 0;
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
        .withHttpRequestCmd ("POST")
        .withExtraHeaders ("Accept: application/json\r\nContent-Type: application/x-www-form-urlencoded\r\n")
        .withConnectionTimeoutMs (10000)
        .withNumRedirectsToFollow (0)
        .withStatusCode (&statusCode));
    if (stream == nullptr)
        return fail ("Cannot confirm the license request. Check your internet connection; check existing activations before retrying.");
    if (statusCode == 429)
        return fail ("Lemon Squeezy is rate-limiting requests. Wait before trying again.");
    constexpr int maximumBytes = 64 * 1024;
    juce::MemoryBlock response;
    char buffer[2048];
    while (! stream->isExhausted())
    {
        if (cancelled())
            return fail ("License request cancelled; check existing activations before retrying.");
        if (juce::Time::getMillisecondCounterHiRes() > deadline)
            return fail ("License response timed out. Check existing activations before retrying.");
        const int read = stream->read (buffer, (int) sizeof (buffer));
        if (read <= 0)
            break;
        if (response.getSize() + (size_t) read > maximumBytes)
            return fail ("The licensing service returned an oversized response.");
        response.append (buffer, (size_t) read);
    }
    if (response.getSize() == 0)
        return fail ("The licensing service returned no response. Check existing activations before retrying.");
    return { juce::Result::ok(), statusCode, response.toString() };
}

} // namespace drumbreaker::licensing