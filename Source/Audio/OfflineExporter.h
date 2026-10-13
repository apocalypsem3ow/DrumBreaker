#pragma once

#include "BreakEngine.h"
#include "ToneStack.h"
#include "../Licensing/LicenseAccess.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace drumbreaker
{

class OfflineExporter
{
public:
    struct Snapshot
    {
        licensing::LicenseAccess license;
        BreakData::Ptr data;
        ToneStack::EQSettings eq;
        ToneStack::FilterValues filter;
        ToneStack::FilterMotion motion = ToneStack::FilterMotion::none;
        double sampleRate = 44100.0;
        int channels = 2;
        float pitchSemitones = 0.0f;
        float filterDrive = 1.0f;
        float filterMix = 1.0f;
        float modulation = 0.0f;
        float reverb = 0.0f;
        float outputDb = 0.0f;
        bool eqEnabled = true;
        bool filterEnabled = true;
    };

    static juce::Result writeWav (const Snapshot&, const juce::File& destination,
                                  const std::function<bool()>& cancelled = {});
};

} // namespace drumbreaker