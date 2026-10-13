#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "../Licensing/LicenseClient.h"

namespace drumbreaker
{

class ActivationPanel : public juce::Component,
                        private juce::Timer
{
public:
    explicit ActivationPanel (licensing::LicenseClient&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void showActivation();
    void showIfUnverified();

    std::function<void()> onDismiss;

private:
    void attemptActivation();
    void dismiss();
    void timerCallback() override;

    licensing::LicenseClient& client;
    juce::Label titleLabel, subtitleLabel, keyLabel, statusLabel;
    juce::TextEditor keyEditor;
    juce::TextButton activateButton { "ACTIVATE" };
    juce::TextButton dismissButton { "Not now" };
    juce::Rectangle<int> panelBounds;
    bool busy = false;
    bool verified = false;
    bool pendingAutomaticPrompt = false;
    juce::String displayedStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActivationPanel)
};

} // namespace drumbreaker