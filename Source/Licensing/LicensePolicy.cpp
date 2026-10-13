#include "LicensePolicy.h"

namespace drumbreaker::licensing
{

CheckedReply checkReply (int httpStatus, const juce::String& json, ReplyKind kind,
                         const juce::String& expectedKey, const juce::String& expectedDevice,
                         const juce::String& expectedInstance, juce::Time now)
{
    auto reject = [] (const char* message)
    {
        return CheckedReply { juce::Result::fail (message), {} };
    };
    if (httpStatus < 200 || httpStatus >= 300)
        return reject ("The licensing service did not return a successful response.");
    if (expectedKey.isEmpty() || (kind != ReplyKind::preflight && expectedDevice.isEmpty())
        || (kind == ReplyKind::validation && expectedInstance.isEmpty()))
        return reject ("License or activation-instance information is missing.");

    juce::var reply;
    if (juce::JSON::parse (json, reply).failed() || ! reply.isObject())
        return reject ("The licensing service returned an invalid response.");
    const auto success = reply.getProperty (kind == ReplyKind::activation ? "activated" : "valid", {});
    if (! success.isBool() || ! (bool) success)
        return reject ("The licensing service did not approve this license.");
    const auto error = reply.getProperty ("error", {});
    if (! reply.getDynamicObject()->hasProperty ("error") || (! error.isVoid() && ! error.isUndefined()))
        return reject ("The licensing service reported an error.");

    const auto meta = reply.getProperty ("meta", {});
    const auto product = meta.getProperty ("product_id", {});
    const auto variant = meta.getProperty ("variant_id", {});
    if ((! product.isInt() && ! product.isInt64()) || (! variant.isInt() && ! variant.isInt64())
        || (juce::int64) product != productId || (juce::int64) variant != variantId)
        return reject ("This license is not for this DrumBreaker product and variant.");

    const auto license = reply.getProperty ("license_key", {});
    const auto status = license.getProperty ("status", {}).toString();
    if (! license.isObject() || (status != "active" && (kind != ReplyKind::preflight || status != "inactive")))
        return reject ("This license is inactive, expired, or disabled.");
    const auto returnedKey = license.getProperty ("key", {});
    if (! returnedKey.isString() || returnedKey.toString() != expectedKey)
        return reject ("The response does not match the submitted license.");
    if (! license.getDynamicObject()->hasProperty ("expires_at"))
        return reject ("License expiration information is missing.");
    const auto expiration = license.getProperty ("expires_at", {});
    if (! expiration.isVoid() && ! expiration.isUndefined())
    {
        if (! expiration.isString())
            return reject ("License expiration information is invalid.");
        const auto expires = juce::Time::fromISO8601 (expiration.toString());
        if (expires.toMilliseconds() <= 0 || expires <= now)
            return reject ("This license has expired or has an invalid expiration date.");
    }

    if (kind == ReplyKind::preflight)
        return { juce::Result::ok(), {} };

    const auto instance = reply.getProperty ("instance", {});
    const auto instanceId = instance.getProperty ("id", {});
    const auto deviceName = instance.getProperty ("name", {});
    if (! instance.isObject() || ! instanceId.isString() || instanceId.toString().trim().isEmpty()
        || ! deviceName.isString() || deviceName.toString() != expectedDevice)
        return reject ("The activation instance does not match this device.");
    if (kind == ReplyKind::validation && instanceId.toString() != expectedInstance)
        return reject ("The response does not match the saved activation instance.");

    return { juce::Result::ok(), instanceId.toString() };
}

} // namespace drumbreaker::licensing