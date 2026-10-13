#pragma once

#include <juce_core/juce_core.h>

namespace drumbreaker::licensing
{

inline constexpr int productId = 1412490;
inline constexpr int variantId = 2205337;
inline constexpr int configuredActivationLimit = 3;

enum class ReplyKind { preflight, activation, validation };

struct CheckedReply
{
    juce::Result result = juce::Result::fail ("License has not been verified.");
    juce::String instanceId;
};

CheckedReply checkReply (int httpStatus, const juce::String& json, ReplyKind,
                         const juce::String& expectedKey, const juce::String& expectedDevice,
                         const juce::String& expectedInstance = {},
                         juce::Time now = juce::Time::getCurrentTime());

} // namespace drumbreaker::licensing