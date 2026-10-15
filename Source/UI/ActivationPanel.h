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
    /** Called once when a license key entered by the user activates successfully. */
    std::function<void()> onActivated;

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
    bool awaitingUserActivation = false;
    juce::String displayedStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActivationPanel)
};

} // namespace drumbreaker