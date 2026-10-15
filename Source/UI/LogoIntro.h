#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace drumbreaker
{

class LogoIntro : public juce::Component,
                  private juce::AsyncUpdater,
                  private juce::Timer
{
public:
    LogoIntro();
    ~LogoIntro() override;
    /** Starts playback. The video is muted unless withSound is true. */
    juce::Result start (bool withSound = false);
    void dismiss();
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void(const juce::String&)> onFinished;

private:
    class Browser;
    void finish (const juce::String& error);
    void handleAsyncUpdate() override;
    void timerCallback() override;
    juce::WebBrowserComponent::Options makeOptions (bool withSound);

    std::unique_ptr<Browser> browser;
    juce::String pendingError;
    double deadline = 0.0;
    bool finishing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LogoIntro)
};

} // namespace drumbreaker
