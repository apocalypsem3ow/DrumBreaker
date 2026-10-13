#include "HintBroadcaster.h"

namespace drumbreaker
{

juce::String HintBroadcaster::findTooltip (juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent())
        if (auto* client = dynamic_cast<juce::TooltipClient*> (c))
        {
            const auto tip = client->getTooltip();

            if (tip.isNotEmpty())
                return tip;
        }

    return {};
}

void HintBroadcaster::setHint (const juce::String& newHint)
{
    if (hint == newHint)
        return;

    hint = newHint;
    sendChangeMessage();

    if (processor != nullptr && hint.isNotEmpty())
        processor->updateHostDisplay();
}

void HintBroadcaster::mouseEnter (const juce::MouseEvent& e)
{
    setHint (findTooltip (e.eventComponent));
}

void HintBroadcaster::mouseExit (const juce::MouseEvent&)
{
    clearHint();
}

} // namespace drumbreaker
