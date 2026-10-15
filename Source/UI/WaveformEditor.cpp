#include "WaveformEditor.h"

#include <algorithm>
#include <cmath>

namespace drumbreaker
{

//==============================================================================
/** Dimmed, floating copy of the fragment being dragged. Lives on the top level
    component so it can follow the mouse anywhere inside the plugin window.
*/
class WaveformEditor::GhostOverlay : public juce::Component
{
public:
    GhostOverlay()
    {
        setInterceptsMouseClicks (false, false);
        setAlwaysOnTop (true);
    }

    void setImage (juce::Image newImage)
    {
        image = std::move (newImage);
        repaint();
    }

    void setAlphaLevel (float a)
    {
        alpha = juce::jlimit (0.0f, 1.0f, a);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (! image.isValid())
            return;

        auto bounds = getLocalBounds().toFloat();

        g.setColour (emerald::accent.withAlpha (0.18f * alpha));
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setOpacity (alpha * 0.65f);
        g.drawImage (image, bounds);

        g.setOpacity (1.0f);
        g.setColour (emerald::accentBright.withAlpha (0.55f * alpha));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
    }

private:
    juce::Image image;
    float alpha = 0.0f;
};

//==============================================================================
WaveformEditor::WaveformEditor()
{
    setWantsKeyboardFocus (true);
    setTooltip ("Waveform: drag fragments to re-arrange, right-click to delete, "
                "scroll to move, Ctrl/Cmd + scroll to zoom");

    addAndMakeVisible (scrollBar);
    scrollBar.addListener (this);
    scrollBar.setAutoHide (false);

    startTimerHz (60);
}

WaveformEditor::~WaveformEditor()
{
    scrollBar.removeListener (this);

    if (ghost != nullptr && ghost->getParentComponent() != nullptr)
        ghost->getParentComponent()->removeChildComponent (ghost.get());
}

void WaveformEditor::setData (BreakData::Ptr newData)
{
    data = std::move (newData);
    selectedEntry = -1;
    hoveredEntry = -1;
    dragEntry = -1;
    scrollSamples = 0.0;
    zoom = 1.0;
    scaleLength = data != nullptr ? (double) data->totalLength : 0.0;
    if (compactMode && data != nullptr && ! data->arrangement.empty())
    {
        const int firstSlice = data->arrangement.front();
        if (juce::isPositiveAndBelow (firstSlice, (int) data->slices.size()))
            scaleLength = (double) data->slices[(size_t) firstSlice].length();
    }
    updateScrollBar();
    repaint();
}

void WaveformEditor::setBpm (double bpm)
{
    if (! juce::approximatelyEqual (bpm, detectedBpm))
    {
        detectedBpm = bpm;
        repaint();
    }
}

void WaveformEditor::setPlayhead (float normalised)
{
    if (std::abs (normalised - playhead) > 1.0e-4f)
    {
        playhead = normalised;
        repaint();
    }
}

void WaveformEditor::setDragHighlight (bool shouldHighlight)
{
    if (dragHighlight != shouldHighlight)
    {
        dragHighlight = shouldHighlight;
        repaint();
    }
}

void WaveformEditor::setStatusMessage (const juce::String& message)
{
    statusMessage = message;
    repaint();
}

void WaveformEditor::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void WaveformEditor::setCompactMode (bool shouldBeCompact)
{
    if (compactMode != shouldBeCompact)
    {
        compactMode = shouldBeCompact;
        resized();
        repaint();
    }
}

void WaveformEditor::setFixedPixelsPerSample (double pps)
{
    pps = juce::jmax (0.0, pps);
    if (juce::approximatelyEqual (fixedPixelsPerSample, pps))
        return;
    fixedPixelsPerSample = pps;
    repaint();
    updateScrollBar();
}

int WaveformEditor::getPreferredContentWidth (double samples, int minimumWidth, int maximumWidth) const
{
    const auto area = waveArea();
    const double pps = pixelsPerSample();
    const int horizontalInsets = juce::roundToInt ((float) getWidth() - area.getWidth());
    return juce::jlimit (minimumWidth, maximumWidth,
                         juce::roundToInt ((float) juce::jmax (0.0, samples) * (float) pps)
                             + horizontalInsets);
}

void WaveformEditor::setIncomingFragment (BreakData::Ptr source, int sliceIndex, float x)
{
    if (source == nullptr || data == nullptr || source->audio != data->audio
        || ! juce::isPositiveAndBelow (sliceIndex, (int) source->slices.size()))
    {
        clearIncomingFragment();
        return;
    }

    const bool wasIncoming = hasIncoming();
    const int newIndex = insertionIndexFor (x);
    if (wasIncoming && incomingData == source && incomingSlice == sliceIndex && incomingIndex == newIndex)
        return;

    incomingData = std::move (source);
    incomingSlice = sliceIndex;
    incomingIndex = newIndex;
    if (! wasIncoming)
        updateScrollBar();
    repaint();
}

void WaveformEditor::clearIncomingFragment()
{
    if (incomingData != nullptr)
    {
        incomingData = nullptr;
        incomingSlice = -1;
        incomingIndex = -1;
        updateScrollBar();
        repaint();
    }
}

bool WaveformEditor::hasIncoming() const
{
    return incomingData != nullptr && data != nullptr && incomingData->audio == data->audio
        && juce::isPositiveAndBelow (incomingSlice, (int) incomingData->slices.size());
}

double WaveformEditor::getIncomingLength() const
{
    return hasIncoming() ? (double) incomingData->slices[(size_t) incomingSlice].length() : 0.0;
}

double WaveformEditor::contentLength() const
{
    return (data != nullptr ? (double) data->totalLength : 0.0) + getIncomingLength();
}

//==============================================================================
juce::Rectangle<float> WaveformEditor::waveArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (14.0f).reduced (6.0f, 6.0f);
}

double WaveformEditor::pixelsPerSample() const
{
    if (data == nullptr || (data->totalLength <= 0 && fixedPixelsPerSample <= 0.0))
        return 0.0;

    if (fixedPixelsPerSample > 0.0)
        return fixedPixelsPerSample * zoom;

    return (double) waveArea().getWidth() * zoom / juce::jmax (1.0, scaleLength);
}

juce::Rectangle<float> WaveformEditor::baseBoundsForEntry (int entryIndex) const
{
    if (data == nullptr || ! juce::isPositiveAndBelow (entryIndex, (int) data->starts.size()))
        return {};

    const auto area = waveArea();
    const double pps = pixelsPerSample();
    const auto& slice = data->slices[(size_t) data->arrangement[(size_t) entryIndex]];

    const float x = area.getX() + (float) (((double) data->starts[(size_t) entryIndex] - scrollSamples) * pps);
    const float w = (float) ((double) slice.length() * pps);

    return { x, area.getY(), juce::jmax (1.0f, w), area.getHeight() };
}

juce::Rectangle<float> WaveformEditor::boundsForEntry (int entryIndex) const
{
    auto bounds = baseBoundsForEntry (entryIndex);

    // Entries after a hovering fragment slide right to open a gap for it.
    if (hasIncoming() && entryIndex >= incomingIndex)
        bounds.translate ((float) (getIncomingLength() * pixelsPerSample()), 0.0f);

    return bounds;
}

int WaveformEditor::entryAt (juce::Point<float> position) const
{
    if (data == nullptr)
        return -1;

    for (int i = 0; i < (int) data->starts.size(); ++i)
        if (boundsForEntry (i).contains (position))
            return i;

    return -1;
}

int WaveformEditor::insertionIndexFor (float x) const
{
    if (data == nullptr || data->starts.empty())
        return 0;

    for (int i = 0; i < (int) data->starts.size(); ++i)
    {
        const auto b = baseBoundsForEntry (i);

        if (x < b.getCentreX())
            return i;
    }

    return (int) data->starts.size();
}

//==============================================================================
void WaveformEditor::resized()
{
    scrollBar.setBounds (getLocalBounds().removeFromBottom (12).reduced (6, 2));
    updateScrollBar();
}

void WaveformEditor::updateScrollBar()
{
    const double totalLength = contentLength();
    if (data == nullptr || totalLength <= 0.0)
    {
        ignoreScrollCallback = true;
        scrollBar.setRangeLimits (0.0, 1.0, juce::dontSendNotification);
        scrollBar.setCurrentRange (0.0, 1.0, juce::dontSendNotification);
        ignoreScrollCallback = false;
        return;
    }

    const double pps = pixelsPerSample();
    const double visible = pps > 0.0 ? (double) waveArea().getWidth() / pps : scaleLength / zoom;
    scrollSamples = juce::jlimit (0.0, juce::jmax (0.0, totalLength - visible), scrollSamples);

    ignoreScrollCallback = true;
    scrollBar.setRangeLimits (0.0, totalLength, juce::dontSendNotification);
    scrollBar.setCurrentRange (scrollSamples, visible, juce::dontSendNotification);
    ignoreScrollCallback = false;
}

void WaveformEditor::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    if (ignoreScrollCallback)
        return;

    scrollSamples = newRangeStart;
    repaint();
}

void WaveformEditor::zoomAround (float mouseX, double factor)
{
    if (data == nullptr || (data->totalLength <= 0 && incomingData == nullptr))
        return;

    const auto area = waveArea();
    const double pps = pixelsPerSample();

    if (pps <= 0.0)
        return;

    const double sampleUnderMouse = scrollSamples + (double) (mouseX - area.getX()) / pps;

    zoom = juce::jlimit (1.0, 400.0, zoom * factor);

    const double newPps = pixelsPerSample();
    scrollSamples = sampleUnderMouse - (double) (mouseX - area.getX()) / newPps;

    updateScrollBar();
    repaint();
}

//==============================================================================
void WaveformEditor::drawWaveformInto (juce::Graphics& g, const Slice& slice,
                                       juce::Rectangle<float> area, juce::Colour colour) const
{
    if (data == nullptr || data->audio == nullptr || area.getWidth() < 1.0f)
        return;

    const auto& audio = *data->audio;
    const int numChannels = juce::jmax (1, audio.getNumChannels());
    const int numSamples  = audio.getNumSamples();
    const auto len = slice.length();

    if (len <= 0 || numSamples <= 0)
        return;

    const int   columns = juce::jmax (1, (int) area.getWidth());
    const float midY    = area.getCentreY();
    const float halfH   = area.getHeight() * 0.46f;

    juce::Path top, bottom;
    top.preallocateSpace (columns * 3 + 8);
    bottom.preallocateSpace (columns * 3 + 8);

    top.startNewSubPath (area.getX(), midY);
    bottom.startNewSubPath (area.getX(), midY);

    for (int c = 0; c < columns; ++c)
    {
        const auto from = slice.start + (juce::int64) ((double) len * (double) c / (double) columns);
        const auto to   = slice.start + (juce::int64) ((double) len * (double) (c + 1) / (double) columns);

        float minVal = 0.0f, maxVal = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* read = audio.getReadPointer (juce::jmin (ch, audio.getNumChannels() - 1));

            for (auto s = from; s < juce::jmin (to, (juce::int64) numSamples); ++s)
            {
                minVal = juce::jmin (minVal, read[s]);
                maxVal = juce::jmax (maxVal, read[s]);
            }
        }

        const float x = area.getX() + (float) c;
        top.lineTo (x, midY - maxVal * halfH);
        bottom.lineTo (x, midY - minVal * halfH);
    }

    top.lineTo (area.getRight(), midY);
    bottom.lineTo (area.getRight(), midY);

    g.setColour (colour.withMultipliedAlpha (0.85f));
    g.strokePath (top, juce::PathStrokeType (1.0f));
    g.strokePath (bottom, juce::PathStrokeType (1.0f));

    g.setColour (colour.withMultipliedAlpha (0.25f));
    g.fillRect (area.withSizeKeepingCentre (area.getWidth(), 1.0f));
}

void WaveformEditor::drawFragment (juce::Graphics& g, int entryIndex, juce::Rectangle<float> area,
                                   float alpha, bool entrySelected) const
{
    if (data == nullptr || ! juce::isPositiveAndBelow (entryIndex, (int) data->arrangement.size()))
        return;

    const auto& slice = data->slices[(size_t) data->arrangement[(size_t) entryIndex]];
    const bool hot = entrySelected || entryIndex == hoveredEntry;

    juce::Path shape;
    shape.addRoundedRectangle (area.reduced (1.0f), 3.0f);

    g.setGradientFill (juce::ColourGradient (emerald::panelLight.withAlpha (alpha * (hot ? 1.0f : 0.8f)),
                                             area.getCentreX(), area.getY(),
                                             emerald::background.withAlpha (alpha),
                                             area.getCentreX(), area.getBottom(), false));
    g.fillPath (shape);

    if (hot)
        EmeraldLookAndFeel::drawGlow (g, shape, emerald::accent, alpha);

    drawWaveformInto (g, slice, area.reduced (2.0f),
                      (hot ? emerald::accentBright : emerald::accent).withMultipliedAlpha (alpha));

    g.setColour (emerald::accent.withAlpha (alpha * (hot ? 0.95f : 0.45f)));
    g.strokePath (shape, juce::PathStrokeType (hot ? 1.6f : 1.0f));

    if (area.getWidth() > 26.0f)
    {
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.setColour (emerald::textDim.withAlpha (alpha));
        g.drawText (juce::String (entryIndex + 1), area.reduced (4.0f, 2.0f),
                    juce::Justification::topLeft, false);
    }
}

void WaveformEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (emerald::background);
    g.fillRoundedRectangle (bounds, 8.0f);

    const auto area = waveArea();

    // centre line + beat grid
    g.setColour (emerald::accentDark.withAlpha (0.25f));
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

    if (data != nullptr && data->totalLength > 0 && detectedBpm > 0.0)
    {
        const double samplesPerBeat = data->sourceSampleRate * 60.0 / detectedBpm;
        const double pps = pixelsPerSample();

        if (samplesPerBeat * pps > 6.0)
        {
            for (double s = std::floor (scrollSamples / samplesPerBeat) * samplesPerBeat;
                 s < (double) data->totalLength; s += samplesPerBeat)
            {
                const float x = area.getX() + (float) ((s - scrollSamples) * pps);

                if (x > area.getRight())
                    break;

                if (x < area.getX())
                    continue;

                const bool barLine = std::fmod (std::round (s / samplesPerBeat), 4.0) == 0.0;
                g.setColour (emerald::accent.withAlpha (barLine ? 0.16f : 0.07f));
                g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            }
        }
    }

    if (data == nullptr || (data->totalLength <= 0 && ! hasIncoming()))
    {
        g.setColour (dragHighlight ? emerald::accentBright : emerald::textDim);
        g.setFont (juce::Font (juce::FontOptions (compactMode ? 10.0f : 18.0f, juce::Font::bold)));
        g.drawText (statusMessage, bounds, juce::Justification::centred, false);
    }
    else
    {
        g.saveState();
        g.reduceClipRegion (area.toNearestInt());

        for (int i = 0; i < (int) data->starts.size(); ++i)
        {
            if (i == dragEntry && draggingOutside)
                continue;

            const auto b = boundsForEntry (i);

            if (b.getRight() < area.getX() || b.getX() > area.getRight())
                continue;

            drawFragment (g, i, b, 1.0f, i == selectedEntry);
        }

        if (hasIncoming())
        {
            const int insertAt = juce::jlimit (0, (int) data->starts.size(), incomingIndex);
            const double pps = pixelsPerSample();
            const auto& slice = incomingData->slices[(size_t) incomingSlice];
            const float x = insertAt < (int) data->starts.size() ? baseBoundsForEntry (insertAt).getX()
                : data->starts.empty() ? area.getX() - (float) (scrollSamples * pps)
                : baseBoundsForEntry ((int) data->starts.size() - 1).getRight();
            juce::Rectangle<float> ghostArea (x, area.getY(), juce::jmax (1.0f, (float) (slice.length() * pps)),
                                              area.getHeight());
            juce::Path ghostShape;
            ghostShape.addRoundedRectangle (ghostArea.reduced (1.0f), 3.0f);
            g.setColour (emerald::accent.withAlpha (0.14f));
            g.fillPath (ghostShape);
            drawWaveformInto (g, slice, ghostArea.reduced (2.0f),
                              emerald::accentBright.withAlpha (0.45f));
            EmeraldLookAndFeel::drawGlow (g, ghostShape, emerald::accentBright, 0.8f);
            g.setColour (emerald::accentBright.withAlpha (0.75f));
            g.strokePath (ghostShape, juce::PathStrokeType (1.5f));
        }

        // drop indicator
        if (dragEntry >= 0 && dropIndex >= 0)
        {
            const float x = dropIndex < (int) data->starts.size() ? boundsForEntry (dropIndex).getX()
                                                                  : boundsForEntry ((int) data->starts.size() - 1).getRight();

            juce::Path marker;
            marker.addRoundedRectangle (x - 1.5f, area.getY(), 3.0f, area.getHeight(), 1.5f);
            EmeraldLookAndFeel::drawGlow (g, marker, emerald::accentBright, 1.0f);
            g.setColour (juce::Colours::white);
            g.fillPath (marker);
        }

        // playhead
        if (playhead > 0.0f)
        {
            const double pps = pixelsPerSample();
            const float x = area.getX() + (float) (((double) playhead * (double) data->totalLength - scrollSamples) * pps);

            if (area.getHorizontalRange().contains (x))
            {
                g.setColour (emerald::accentBright.withAlpha (0.9f));
                g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            }
        }

        g.restoreState();
    }

    // BPM badge, top left of the visualiser
    if (! compactMode)
    {
        const juce::Rectangle<float> badge (area.getX() + 4.0f, area.getY() + 4.0f, 120.0f, 40.0f);

        juce::Path shape;
        shape.addRoundedRectangle (badge, 6.0f);

        g.setColour (emerald::panel.withAlpha (0.92f));
        g.fillPath (shape);
        EmeraldLookAndFeel::drawGlow (g, shape, emerald::accent, 0.6f);
        g.setColour (emerald::accent.withAlpha (0.8f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));

        g.setColour (emerald::textDim);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText ("BPM", badge.reduced (8.0f, 4.0f), juce::Justification::topLeft, false);

        g.setColour (emerald::accentBright);
        g.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold)));
        g.drawText (detectedBpm > 0.0 ? juce::String (detectedBpm, 3) : juce::String ("----"),
                    badge.reduced (8.0f, 4.0f), juce::Justification::bottomLeft, false);

        if (data != nullptr && data->name.isNotEmpty())
        {
            g.setColour (emerald::textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (data->name + "   -   " + juce::String ((int) data->arrangement.size()) + " fragments",
                        area.withTrimmedLeft (badge.getWidth() + 14.0f).removeFromTop (20.0f).translated (0.0f, 6.0f),
                        juce::Justification::topLeft, true);
        }
    }

    const auto outline = selected ? emerald::accentBright : (dragHighlight ? emerald::accentBright : emerald::accent);
    const float outlineAlpha = selected ? 0.95f : (dragHighlight ? 0.95f : 0.35f);
    g.setColour (outline.withAlpha (outlineAlpha));
    if (selected)
    {
        juce::Path glow;
        glow.addRoundedRectangle (bounds.reduced (1.0f), compactMode ? 4.0f : 8.0f);
        EmeraldLookAndFeel::drawGlow (g, glow, emerald::accent, 0.8f);
    }
    g.drawRoundedRectangle (bounds.reduced (0.5f), compactMode ? 4.0f : 8.0f,
                            dragHighlight || selected ? 2.0f : 1.0f);
}

//==============================================================================
void WaveformEditor::mouseMove (const juce::MouseEvent& e)
{
    const int entry = entryAt (e.position);

    if (entry != hoveredEntry)
    {
        hoveredEntry = entry;
        repaint();
    }
}

void WaveformEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoveredEntry != -1)
    {
        hoveredEntry = -1;
        repaint();
    }
}

void WaveformEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (onSelected != nullptr)
        onSelected();

    const int entry = entryAt (e.position);
    selectedEntry = entry;

    if (e.mods.isPopupMenu())
    {
        showContextMenu (entry);
        return;
    }

    if (entry < 0)
    {
        repaint();
        return;
    }

    dragEntry = entry;
    dropIndex = entry;
    draggingOutside = false;
    ghostAlpha = 0.0f;

    if (ghost == nullptr)
        ghost = std::make_unique<GhostOverlay>();

    if (auto* top = getTopLevelComponent())
    {
        if (ghost->getParentComponent() != top)
            top->addChildComponent (ghost.get());
    }

    // Snapshot the fragment so the ghost can be drawn anywhere in the window.
    const auto b = boundsForEntry (entry);
    const int w = juce::jlimit (8, 900, (int) b.getWidth());
    const int h = juce::jlimit (8, 400, (int) b.getHeight());

    juce::Image snapshot (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics ig (snapshot);
        drawFragment (ig, entry, juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), 1.0f, true);
    }

    ghost->setImage (std::move (snapshot));
    ghost->setSize (w, h);
    ghost->setAlphaLevel (0.0f);
    ghost->setVisible (true);

    repaint();
}

void WaveformEditor::updateGhost (const juce::MouseEvent& e)
{
    if (ghost == nullptr)
        return;

    auto* top = getTopLevelComponent();

    if (top == nullptr)
        return;

    const auto pointInTop = top->getLocalPoint (this, e.position);

    // The fragment is magnetically tethered: it can never leave the window, it just
    // presses against the edge and fades while the mouse wanders further away.
    const juce::Rectangle<int> limits (0, 0,
                                       juce::jmax (1, top->getWidth()  - ghost->getWidth()),
                                       juce::jmax (1, top->getHeight() - ghost->getHeight()));

    const juce::Point<int> wanted ((int) pointInTop.x - ghost->getWidth() / 2,
                                   (int) pointInTop.y - ghost->getHeight() / 2);
    const juce::Point<int> clamped (juce::jlimit (limits.getX(), limits.getRight(),  wanted.x),
                                    juce::jlimit (limits.getY(), limits.getBottom(), wanted.y));

    ghost->setTopLeftPosition (clamped);

    const float pull = (float) wanted.getDistanceFrom (clamped);
    ghost->setAlphaLevel (juce::jlimit (0.15f, 0.75f, 0.75f - pull * 0.002f));
}

void WaveformEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragEntry < 0 || data == nullptr)
        return;

    const bool outside = ! waveArea().contains (e.position);

    if (outside != draggingOutside)
    {
        draggingOutside = outside;
        repaint();
    }

    if (draggingOutside)
    {
        updateGhost (e);
    }
    else if (ghost != nullptr)
    {
        ghost->setAlphaLevel (0.0f);
    }

    const int newDrop = insertionIndexFor (juce::jlimit (waveArea().getX(), waveArea().getRight(), e.position.x));

    if (newDrop != dropIndex)
    {
        dropIndex = newDrop;
        repaint();
    }

    if (onFragmentDragged != nullptr)
    {
        auto* top = getTopLevelComponent();
        if (top != nullptr)
        {
            const bool magnetised = onFragmentDragged (data, data->arrangement[(size_t) dragEntry],
                                                        top->getLocalPoint (this, e.position).toInt());
            if (ghost != nullptr)
                ghost->setVisible (! magnetised);
        }
    }

    // auto-scroll when dragging against the edges of a zoomed-in view
    if (zoom > 1.0)
    {
        const auto area = waveArea();

        if (e.position.x < area.getX() + 24.0f)
            scrollSamples -= 12.0 / juce::jmax (1.0e-9, pixelsPerSample());
        else if (e.position.x > area.getRight() - 24.0f)
            scrollSamples += 12.0 / juce::jmax (1.0e-9, pixelsPerSample());

        updateScrollBar();
    }
}

void WaveformEditor::mouseUp (const juce::MouseEvent& e)
{
    if (dragEntry >= 0)
        finishDrag (e);
}

void WaveformEditor::finishDrag (const juce::MouseEvent& e)
{
    if (data == nullptr || dragEntry < 0)
        return;

    // Drop before clearing the hover preview: the preview temporarily widens the
    // target display and the release point must be tested against that width.
    bool dropped = false;
    if (draggingOutside && onFragmentDropped != nullptr)
    {
        auto* top = getTopLevelComponent();
        dropped = top != nullptr && onFragmentDropped (data, data->arrangement[(size_t) dragEntry],
                                                       top->getLocalPoint (this, e.position).toInt());
    }

    if (onFragmentDragged != nullptr)
        onFragmentDragged (nullptr, -1, {});

    if (dropped)
    {
        dragEntry = -1;
        dropIndex = -1;
        draggingOutside = false;
        if (ghost != nullptr)
            ghost->setVisible (false);
        repaint();
        return;
    }

    auto order = data->arrangement;

    if (juce::isPositiveAndBelow (dragEntry, (int) order.size()) && dropIndex >= 0)
    {
        const int moved = order[(size_t) dragEntry];
        order.erase (order.begin() + dragEntry);

        int target = dropIndex;

        if (target > dragEntry)
            --target;

        target = juce::jlimit (0, (int) order.size(), target);
        order.insert (order.begin() + target, moved);
        selectedEntry = target;
    }

    dragEntry = -1;
    dropIndex = -1;
    draggingOutside = false;

    if (ghost != nullptr)
        ghost->setVisible (false);

    if (order != data->arrangement)
    {
        data = data->withArrangement (order);
        if (! compactMode && fixedPixelsPerSample <= 0.0)
            scaleLength = (double) data->totalLength;

        if (onArrangementChanged != nullptr)
            onArrangementChanged (order);

        updateScrollBar();
    }

    repaint();
}

void WaveformEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (data == nullptr || data->totalLength <= 0)
        return;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomAround (e.position.x, wheel.deltaY > 0.0f ? 1.15 : 1.0 / 1.15);
        return;
    }

    const double pps = pixelsPerSample();

    if (pps > 0.0)
    {
        scrollSamples -= (double) (wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * 120.0 / pps;
        updateScrollBar();
        repaint();
    }
}

bool WaveformEditor::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedEntry >= 0)
    {
        deleteEntry (selectedEntry);
        return true;
    }

    return false;
}

//==============================================================================
void WaveformEditor::commitArrangement()
{
    if (data == nullptr)
        return;

    if (! compactMode && fixedPixelsPerSample <= 0.0)
        scaleLength = (double) data->totalLength;
    else if (data->totalLength <= 0)
        scaleLength = 0.0;

    updateScrollBar();

    if (onArrangementChanged != nullptr)
        onArrangementChanged (data->arrangement);

    repaint();
}

void WaveformEditor::deleteEntry (int entryIndex)
{
    if (data == nullptr || ! juce::isPositiveAndBelow (entryIndex, (int) data->arrangement.size()))
        return;

    auto order = data->arrangement;
    order.erase (order.begin() + entryIndex);

    // The timeline is rebuilt from the remaining fragments, so the display and the
    // rendered output both shrink: no silent gap is ever left behind.
    data = data->withArrangement (order);
    selectedEntry = juce::jmin (entryIndex, (int) order.size() - 1);
    hoveredEntry = -1;

    commitArrangement();
}

void WaveformEditor::duplicateEntry (int entryIndex)
{
    if (data == nullptr || ! juce::isPositiveAndBelow (entryIndex, (int) data->arrangement.size()))
        return;

    auto order = data->arrangement;
    order.insert (order.begin() + entryIndex, order[(size_t) entryIndex]);

    data = data->withArrangement (order);
    commitArrangement();
}

void WaveformEditor::addFragmentFrom (BreakData::Ptr source, int sliceIndex, float x)
{
    if (data == nullptr || source == nullptr || source->audio != data->audio
        || ! juce::isPositiveAndBelow (sliceIndex, (int) source->slices.size()))
        return;

    const auto slice = source->slices[(size_t) sliceIndex];
    const int insertAt = insertionIndexFor (x);
    incomingData = nullptr;
    incomingSlice = -1;
    incomingIndex = -1;
    auto found = std::find_if (data->slices.begin(), data->slices.end(), [&slice] (const Slice& existing)
    {
        return existing.start == slice.start && existing.end == slice.end;
    });

    std::vector<int> order = data->arrangement;
    int destinationSlice = -1;
    if (found != data->slices.end())
    {
        destinationSlice = (int) std::distance (data->slices.begin(), found);
        order.insert (order.begin() + insertAt, destinationSlice);
        data = data->withArrangement (order);
    }
    else
    {
        destinationSlice = (int) data->slices.size();
        order.insert (order.begin() + insertAt, destinationSlice);
        data = data->withSliceAndArrangement (slice, order);
    }

    if (scaleLength <= 0.0)
        scaleLength = (double) slice.length();

    selectedEntry = insertAt;
    commitArrangement();
}

void WaveformEditor::showContextMenu (int entryIndex)
{
    if (data == nullptr)
        return;

    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addItem (1, "Delete fragment", entryIndex >= 0);
    menu.addItem (2, "Duplicate fragment", entryIndex >= 0);
    menu.addSeparator();
    menu.addItem (3, "Reset zoom");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this, entryIndex] (int result)
                        {
                            if (result == 1) deleteEntry (entryIndex);
                            else if (result == 2) duplicateEntry (entryIndex);
                            else if (result == 3) { zoom = 1.0; scrollSamples = 0.0; updateScrollBar(); repaint(); }
                        });
}

void WaveformEditor::timerCallback()
{
    if (dragEntry >= 0 && draggingOutside && ghost != nullptr)
        ghost->repaint();
}

} // namespace drumbreaker
