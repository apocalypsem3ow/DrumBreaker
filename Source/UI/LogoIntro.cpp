#include "LogoIntro.h"
#include <DrumBreakerIntroData.h>

namespace drumbreaker
{

class LogoIntro::Browser : public juce::WebBrowserComponent
{
public:
    Browser (LogoIntro& intro, const Options& options) : WebBrowserComponent (options), owner (intro) {}

    bool pageAboutToLoad (const juce::String& url) override
    {
        return url == getResourceProviderRoot() || url == "about:blank";
    }

    bool pageLoadHadNetworkError (const juce::String& error) override
    {
        owner.finish (error);
        return false;
    }

private:
    LogoIntro& owner;
};

LogoIntro::LogoIntro()
{
    setOpaque (true);
    setVisible (false);
}

LogoIntro::~LogoIntro()
{
    stopTimer();
    cancelPendingUpdate();
    browser.reset();
}

juce::WebBrowserComponent::Options LogoIntro::makeOptions (bool withSound)
{
    auto options = juce::WebBrowserComponent::Options {}
        .withNativeIntegrationEnabled()
        .withInitialisationData ("introSound", withSound)
        .withEventListener ("introFinished", [this] (const juce::var& error) { finish (error.toString()); })
        .withEventListener ("introPlaying", [this] (const juce::var&)
        {
            deadline = juce::Time::getMillisecondCounterHiRes() + 60000.0;
        })
        .withResourceProvider ([] (const juce::String& path)
            -> std::optional<juce::WebBrowserComponent::Resource>
        {
            const char* data = nullptr;
            int size = 0;
            juce::String mime;
            if (path == "/")
            {
                data = DrumBreakerIntroData::LogoIntro_html;
                size = DrumBreakerIntroData::LogoIntro_htmlSize;
                mime = "text/html";
            }
            else if (path == "/LogoIntro.mp4")
            {
                data = DrumBreakerIntroData::LogoIntro_mp4;
                size = DrumBreakerIntroData::LogoIntro_mp4Size;
                mime = "video/mp4";
            }
            else
                return std::nullopt;

            const auto* begin = reinterpret_cast<const std::byte*> (data);
            return juce::WebBrowserComponent::Resource { { begin, begin + size }, mime };
        });
   #if JUCE_WINDOWS
    options = options.withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
            .withUserDataFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                .getChildFile ("DrumBreaker").getChildFile ("LogoWebView"))
            .withStatusBarDisabled()
            .withBuiltInErrorPageDisabled()
            .withBackgroundColour (juce::Colours::black));
   #endif
    return options;
}

juce::Result LogoIntro::start (bool withSound)
{
    const auto options = makeOptions (withSound);
    if (! juce::WebBrowserComponent::areOptionsSupported (options))
        return juce::Result::fail ("Logo intro requires the Microsoft Edge WebView2 Runtime.");

    finishing = false;
    pendingError.clear();
    browser = std::make_unique<Browser> (*this, options);
    addAndMakeVisible (*browser);
    setVisible (true);
    toFront (false);
    resized();
    deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    startTimer (100);
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    return juce::Result::ok();
}

void LogoIntro::finish (const juce::String& error)
{
    if (finishing)
        return;
    finishing = true;
    pendingError = error;
    stopTimer();
    // Native browser callbacks must return before destroying the browser.
    triggerAsyncUpdate();
}

void LogoIntro::handleAsyncUpdate()
{
    dismiss();
    if (onFinished != nullptr)
        onFinished (pendingError);
}

void LogoIntro::dismiss()
{
    stopTimer();
    cancelPendingUpdate();
    finishing = true;
    browser.reset();
    setVisible (false);
}

void LogoIntro::timerCallback()
{
    if (juce::Time::getMillisecondCounterHiRes() > deadline)
        finish ("Logo intro playback timed out.");
}

void LogoIntro::resized()
{
    if (browser != nullptr)
        browser->setBounds (getLocalBounds());
}

void LogoIntro::paint (juce::Graphics& graphics) { graphics.fillAll (juce::Colours::black); }
void LogoIntro::mouseDown (const juce::MouseEvent&) { finish ({}); }

} // namespace drumbreaker
