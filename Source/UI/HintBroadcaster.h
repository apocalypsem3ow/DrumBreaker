#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace drumbreaker
{

/** Collects the tooltip of whatever the mouse is over and republishes it:
      - to the plugin's own hint strip, and
      - to the host, via updateHostDisplay(), so hint panels such as FL Studio's
        pick the text up together with the parameter name and value.
*/
class HintBroadcaster : public juce::ChangeBroadcaster,
                        public juce::MouseListener
{
public:
    HintBroadcaster() = default;

    void setHostProcessor (juce::AudioProcessor* p) noexcept { processor = p; }

    juce::String getHint() const { return hint; }

    void setHint (const juce::String& newHint);
    void clearHint() { setHint ({}); }

    /** Attach to a top level component: every child is then tracked automatically. */
    void attachTo (juce::Component& root) { root.addMouseListener (this, true); }
    void detachFrom (juce::Component& root) { root.removeMouseListener (this); }

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;

private:
    static juce::String findTooltip (juce::Component*);

    juce::String hint;
    juce::AudioProcessor* processor = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HintBroadcaster)
};

} // namespace drumbreaker
