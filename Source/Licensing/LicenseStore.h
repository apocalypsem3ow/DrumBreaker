#pragma once

#include "LicensePolicy.h"

namespace drumbreaker::licensing
{

struct StoredActivation
{
    juce::String key, device, instance;
};

class LicenseStore
{
public:
    explicit LicenseStore (juce::String storageScope = "production");
    juce::Result load (StoredActivation&, bool& found) const;
    juce::Result save (const StoredActivation&) const;
    juce::Result remove() const;

private:
    const juce::String target;
};

} // namespace drumbreaker::licensing