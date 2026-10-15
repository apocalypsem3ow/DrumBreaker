#pragma once

#include "Audio/BreakEngine.h"
#include "UI/EmeraldLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace drumbreaker
{

/** Zoomable, scrollable waveform "playlist" of the broken-up drum loop.

    Fragments can be dragged to re-order them. A fragment dragged away from the
    display follows the mouse as a dimmed ghost and is magnetically pulled back
    into the nearest slot when it is released, wherever the mouse ends up.
*/
class WaveformEditor : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer,
                       private juce::ScrollBar::Listener
{
public:
    WaveformEditor();
    ~WaveformEditor() override;

    void setData (BreakData::Ptr newData);
    void setBpm (double bpm);
    void setPlayhead (float normalised);
    void setDragHighlight (bool shouldHighlight);
    void setStatusMessage (const juce::String& message);
    void setSelected (bool shouldBeSelected);
    void setCompactMode (bool shouldBeCompact);
    void setFixedPixelsPerSample (double pixelsPerSample);
    double getPixelsPerSample() const { return pixelsPerSample(); }
    int getPreferredContentWidth (double samples, int minimumWidth, int maximumWidth) const;
    bool acceptsFragmentDropAt (juce::Point<float> position) const { return waveArea().contains (position); }
    void acceptFragmentDrop (BreakData::Ptr source, int sliceIndex, float x) { addFragmentFrom (std::move (source), sliceIndex, x); }
    void setIncomingFragment (BreakData::Ptr source, int sliceIndex, float x);
    void clearIncomingFragment();
    /** Length in samples of the fragment currently hovering over this display (0 if none). */
    double getIncomingLength() const;

    /** Called whenever the user re-orders or deletes fragments. */
    std::function<void (const std::vector<int>&)> onArrangementChanged;
    std::function<void()> onSelected;
    std::function<bool (BreakData::Ptr, int, juce::Point<int>)> onFragmentDropped;
    std::function<bool (BreakData::Ptr, int, juce::Point<int>)> onFragmentDragged;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class GhostOverlay;

    struct EntryBounds
    {
        int entryIndex = -1;
        juce::Rectangle<float> area;
    };

    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    juce::Rectangle<float> waveArea() const;
    double pixelsPerSample() const;
    int    entryAt (juce::Point<float> position) const;
    int    insertionIndexFor (float x) const;
    bool   hasIncoming() const;
    double contentLength() const;
    juce::Rectangle<float> baseBoundsForEntry (int entryIndex) const;
    juce::Rectangle<float> boundsForEntry (int entryIndex) const;

    void drawFragment (juce::Graphics&, int entryIndex, juce::Rectangle<float> area,
                       float alpha, bool selected) const;
    void drawWaveformInto (juce::Graphics&, const Slice&, juce::Rectangle<float> area, juce::Colour) const;

    void updateScrollBar();
    void zoomAround (float mouseX, double factor);
    void commitArrangement();
    void deleteEntry (int entryIndex);
    void duplicateEntry (int entryIndex);
    void showContextMenu (int entryIndex);
    void updateGhost (const juce::MouseEvent&);
    void finishDrag (const juce::MouseEvent&);
    void addFragmentFrom (BreakData::Ptr source, int sliceIndex, float x);

    BreakData::Ptr data;
    double detectedBpm = 0.0;
    float  playhead = 0.0f;
    bool   dragHighlight = false;
    bool   selected = false;
    bool   compactMode = false;
    juce::String statusMessage { "Drop a drum loop here" };
    double scaleLength = 0.0;

    double zoom = 1.0;            // 1.0 = whole arrangement fits the view
    double scrollSamples = 0.0;   // left edge of the view, in arrangement samples
    double fixedPixelsPerSample = 0.0;
    BreakData::Ptr incomingData;
    int incomingSlice = -1;
    int incomingIndex = -1;

    int hoveredEntry = -1;
    int selectedEntry = -1;
    int dragEntry = -1;
    int dropIndex = -1;
    bool draggingOutside = false;
    float ghostAlpha = 0.0f;

    std::unique_ptr<GhostOverlay> ghost;
    juce::ScrollBar scrollBar { false };
    bool ignoreScrollCallback = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformEditor)
};

} // namespace drumbreaker
