#include "PluginEditor.h"
#include "ParameterIDs.h"
#include "Audio/FilterPresets.h"

namespace drumbreaker
{

static ToneStack::FilterMotion filterMotionForPreset (int preset)
{
    switch (preset)
    {
        case 1: return ToneStack::FilterMotion::lowPassSweep;
        case 2: return ToneStack::FilterMotion::bandPassSweep;
        case 3: return ToneStack::FilterMotion::highPassSweep;
        default: return ToneStack::FilterMotion::none;
    }
}

int PianoRoll::noteForTypingKey (int keyCode) noexcept
{
    const int key = (int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) keyCode);
    constexpr char lower[] = "zsxdcvgbhnjm";
    constexpr char upper[] = "q2w3er5t6y7ui9o0p";
    for (int offset = 0; offset < 17; ++offset)
    {
        if (offset < 12 && key == lower[offset])
            return 48 + offset;
        if (key == upper[offset])
            return 60 + offset;
    }
    return -1;
}

PianoRoll::PianoRoll (juce::MidiKeyboardState& state, Orientation orientation)
    : juce::MidiKeyboardComponent (state, orientation)
{
    clearKeyMappings();
    setKeyPressBaseOctave (4);
    for (int key = 32; key < 127; ++key)
        if (const int note = noteForTypingKey (key); note >= 0)
            setKeyPressForNote (juce::KeyPress (juce::CharacterFunctions::toUpperCase (
                (juce::juce_wchar) key), 0, 0), note - 48);
}

void ActivationStatusButton::setLicenseVerified (bool verified)
{
    if (licenseVerified == verified)
        return;
    licenseVerified = verified;
    setButtonText (verified ? "ACTIVATED" : "ACTIVATE");
    setTooltip (verified ? "License verified. Click to view license status." : "Open DrumBreaker license activation");
    repaint();
}

void ActivationStatusButton::paintButton (juce::Graphics& graphics, bool highlighted, bool down)
{
    if (! licenseVerified)
    {
        juce::TextButton::paintButton (graphics, highlighted, down);
        return;
    }
    getLookAndFeel().drawButtonBackground (graphics, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);
    auto textBounds = getLocalBounds().reduced (6, 2);
    const auto icon = textBounds.removeFromRight (16).toFloat();
    graphics.setColour (emerald::accentBright);
    graphics.setFont (juce::Font (juce::FontOptions (juce::jmin (16.0f, (float) getHeight() * 0.42f), juce::Font::bold)));
    graphics.drawFittedText ("ACTIVATED", textBounds, juce::Justification::centred, 1);
    auto tick = getLookAndFeel().getTickShape (10.0f);
    tick.scaleToFit (icon.getX() + 2.0f, icon.getCentreY() - 5.0f, 12.0f, 10.0f, true);
    graphics.setColour (emerald::accentBright);
    graphics.fillPath (tick);
}

class EQMenuItem : public juce::TextButton
{
public:
    bool checked = false;

    void paintButton (juce::Graphics& graphics, bool highlighted, bool down) override
    {
        if (isEnabled() && (highlighted || down))
        {
            graphics.setColour (emerald::accentDark.withAlpha (down ? 0.65f : 0.4f));
            graphics.fillRect (getLocalBounds());
        }
        graphics.setColour (highlighted && isEnabled() ? emerald::accentBright : emerald::text);
        graphics.setFont (juce::Font (juce::FontOptions (11.0f)));
        auto textBounds = getLocalBounds().reduced (8, 0);
        textBounds.removeFromLeft (checked ? 14 : 0);
        graphics.drawText (getButtonText(), textBounds, juce::Justification::centredLeft);
        if (checked)
        {
            graphics.setColour (emerald::accent);
            graphics.fillEllipse (7.0f, (float) getHeight() * 0.5f - 2.5f, 5.0f, 5.0f);
        }
    }
};

class ExportMenu : public juce::Component,
                   private juce::Timer
{
public:
    ExportMenu (juce::Component& anchorComponent, std::function<void()> playlistAction,
                std::function<void()> folderAction)
        : anchor (&anchorComponent)
    {
        playlist.setButtonText ("EXPORT TO PLAYLIST...");
        playlist.setTooltip ("Render a WAV, then drag the rendered filename onto your DAW's Playlist/timeline. No automatic insertion.");
        folder.setButtonText ("EXPORT TO FOLDER...");
        playlist.onClick = [this, action = std::move (playlistAction)] { close(); action(); };
        folder.onClick = [this, action = std::move (folderAction)] { close(); action(); };
        addAndMakeVisible (playlist);
        addAndMakeVisible (folder);
        setSize (220, 68);
        startTimerHz (60);
    }

    void resized() override
    {
        playlist.setBounds (4, 4, getWidth() - 8, 30);
        folder.setBounds (4, 34, getWidth() - 8, 30);
    }

    void paint (juce::Graphics& graphics) override
    {
        graphics.setColour (emerald::panel);
        graphics.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        graphics.setColour (emerald::accent.withAlpha (0.45f));
        graphics.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    }

private:
    void close() { stopTimer(); setVisible (false); }

    void timerCallback() override
    {
        const auto point = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
        if (getScreenBounds().contains (point))
            entered = true;
        else if (entered || anchor == nullptr || ! anchor->getScreenBounds().contains (point))
            close();
    }

    juce::Component::SafePointer<juce::Component> anchor;
    EQMenuItem playlist, folder;
    bool entered = false;
};

class RenderedFileButton : public juce::TextButton
{
public:
    explicit RenderedFileButton (juce::File rendered) : file (std::move (rendered))
    {
        setButtonText (file.getFileName());
        setTooltip ("Drag this processed WAV onto the Playlist/timeline, or click to reveal it in Explorer/Finder. File: "
                + file.getFullPathName());
        onClick = [this] { file.revealToUser(); };
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        juce::TextButton::mouseDrag (event);
        if (event.mods.isLeftButtonDown() && event.getDistanceFromDragStart() > 4 && ! externalDrag)
        {
            const juce::Component::SafePointer<RenderedFileButton> safeThis (this);
            externalDrag = true;
            const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles (
                { file.getFullPathName() }, false, this, [safeThis]
                {
                    if (safeThis != nullptr)
                    {
                        safeThis->externalDrag = false;
                        safeThis->repaint();
                    }
                });
            if (! started)
            {
                externalDrag = false;
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "WAV transfer", "The system could not start file dragging. Import this WAV in your DAW:\n"
                                     + file.getFullPathName());
            }
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        externalDrag = false;
        juce::TextButton::mouseDown (event);
    }

private:
    juce::File file;
    bool externalDrag = false;
};

class ExportJob : public juce::ThreadPoolJob
{
public:
    ExportJob (OfflineExporter::Snapshot settings, juce::File target,
               std::function<void(juce::Result)> completed)
        : juce::ThreadPoolJob ("Drum Breaker WAV export"), snapshot (std::move (settings)),
          destination (std::move (target)), completion (std::move (completed)) {}

    JobStatus runJob() override
    {
        const auto result = OfflineExporter::writeWav (snapshot, destination, [this] { return shouldExit(); });
        juce::MessageManager::callAsync ([callback = completion, result] { callback (result); });
        return jobHasFinished;
    }

private:
    OfflineExporter::Snapshot snapshot;
    juce::File destination;
    std::function<void(juce::Result)> completion;
};

class EQContextMenu : public juce::Component,
                      private juce::Timer
{
public:
    EQContextMenu (juce::AudioProcessorValueTreeState& parameters, juce::Component& anchorComponent, bool isFilter)
        : state (parameters), anchor (&anchorComponent), filterSection (isFilter)
    {
        toggle.onClick = [this]
        {
            auto* parameter = state.getParameter (powerParameterID());
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (isEQEnabled() ? 0.0f : 1.0f);
            parameter->endChangeGesture();
            updateItems();
        };
        reset.setButtonText ("Reset to Default");
        reset.onClick = [this] { applyPreset (0); };
        presets.setButtonText ("Presets");
        presets.onClick = [this]
        {
            submenuVisible = ! submenuVisible;
            updateItems();
        };
        lowPass.setButtonText (filterSection ? "Low Pass Filter Sweep..." : "Low Pass");
        bandPass.setButtonText (filterSection ? "Band Pass Filter Sweep..." : "Band Pass");
        highPass.setButtonText (filterSection ? "High Pass Filter Sweep..." : "High Pass");
        lowPass.onClick = [this] { applyPreset (1); };
        bandPass.onClick = [this] { applyPreset (2); };
        highPass.onClick = [this] { applyPreset (3); };
        for (auto* button : { &toggle, &reset, &presets, &lowPass, &bandPass, &highPass })
        {
            button->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            addAndMakeVisible (*button);
        }
        setSize (filterSection ? 376 : 312, 156);
        updateItems();
        startTimerHz (60);
    }

    void resized() override
    {
        toggle.setBounds (4, 4, 148, 28);
        reset.setBounds (4, 32, 148, 28);
        presets.setBounds (4, 60, 148, 28);
        lowPass.setBounds (160, 64, getWidth() - 164, 28);
        bandPass.setBounds (160, 92, getWidth() - 164, 28);
        highPass.setBounds (160, 120, getWidth() - 164, 28);
    }

    bool hitTest (int x, int y) override
    {
        return rootBounds().contains (x, y) || (submenuVisible && submenuBounds().contains (x, y));
    }

    void paint (juce::Graphics& graphics) override
    {
        for (auto bounds : { rootBounds(), submenuBounds() })
        {
            if (bounds == submenuBounds() && ! submenuVisible)
                continue;
            graphics.setColour (emerald::panel);
            graphics.fillRoundedRectangle (bounds.toFloat(), 4.0f);
            graphics.setColour (emerald::accent.withAlpha (0.45f));
            graphics.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 4.0f, 1.0f);
        }
        graphics.setColour (isEQEnabled() ? emerald::accent : emerald::textDim.withAlpha (0.3f));
        juce::Path arrow;
        arrow.startNewSubPath (140.0f, 70.0f);
        arrow.lineTo (144.0f, 74.0f);
        arrow.lineTo (140.0f, 78.0f);
        graphics.strokePath (arrow, juce::PathStrokeType (1.2f));
    }

private:
    static juce::Rectangle<int> rootBounds() { return { 0, 0, 156, 92 }; }
    juce::Rectangle<int> submenuBounds() const { return { 156, 60, getWidth() - 156, 96 }; }
    const char* powerParameterID() const { return filterSection ? params::filterEnabled : params::eqEnabled; }
    bool isEQEnabled() const { return state.getRawParameterValue (powerParameterID())->load() >= 0.5f; }

    void applyPreset (int preset)
    {
        if (! isEQEnabled())
            return;
        if (filterSection)
        {
            auto set = [this] (const char* id, float value)
            {
                auto* parameter = state.getParameter (id);
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
                parameter->endChangeGesture();
            };
            const int selected = (int) state.getRawParameterValue (params::filterMotion)->load();
            if (preset == 0)
            {
                for (const char* id : { params::filterType, params::filterCutoff, params::filterReso,
                                        params::filterDrive, params::filterMix, params::filterMotion })
                {
                    auto* parameter = state.getParameter (id);
                    set (id, parameter->convertFrom0to1 (parameter->getDefaultValue()));
                }
            }
            else
            {
                const auto motion = filterMotionForPreset (preset);
                if (selected != (int) motion)
                {
                    set (params::filterMix, 1.0f);
                }
                set (params::filterMotion, selected == (int) motion ? 0.0f : (float) motion);
            }
        }
        else
            applyEQPreset (state, (ToneStack::EQPreset) preset);
        stopTimer();
        setVisible (false);
    }

    void updateItems()
    {
        const bool enabled = isEQEnabled();
        toggle.setButtonText (enabled ? "Off" : "On");
        reset.setEnabled (enabled);
        presets.setEnabled (enabled);
        reset.setAlpha (enabled ? 1.0f : 0.3f);
        presets.setAlpha (enabled ? 1.0f : 0.3f);
        if (! enabled)
            submenuVisible = false;
        for (auto* button : { &lowPass, &bandPass, &highPass })
        {
            button->setVisible (submenuVisible);
            button->setEnabled (enabled);
        }
        if (filterSection)
        {
            const int selected = (int) state.getRawParameterValue (params::filterMotion)->load();
            lowPass.checked = selected == (int) ToneStack::FilterMotion::lowPassSweep;
            bandPass.checked = selected == (int) ToneStack::FilterMotion::bandPassSweep;
            highPass.checked = selected == (int) ToneStack::FilterMotion::highPassSweep;
        }
        repaint();
    }

    void timerCallback() override
    {
        const auto screenPoint = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
        const auto point = getLocalPoint (nullptr, screenPoint);
        const bool overPresets = rootBounds().withTop (presets.getY()).contains (point);
        const bool overSubmenu = submenuVisible && submenuBounds().contains (point);
        const bool overRoot = rootBounds().contains (point);
        if (overRoot || overSubmenu)
            entered = true;
        if ((! overRoot && ! overSubmenu)
            && (entered || anchor == nullptr || ! anchor->getScreenBounds().contains (screenPoint)))
        {
            stopTimer();
            setVisible (false);
            return;
        }
        if (isEQEnabled() && overPresets)
            submenuVisible = true;
        else if (overRoot && ! overPresets)
            submenuVisible = false;
        updateItems();
    }

    juce::AudioProcessorValueTreeState& state;
    juce::Component::SafePointer<juce::Component> anchor;
    const bool filterSection;
    EQMenuItem toggle, reset, presets, lowPass, bandPass, highPass;
    bool submenuVisible = false;
    bool entered = false;
};

EQLamp::EQLamp (juce::AudioProcessorValueTreeState& parameters, bool isFilter)
    : state (parameters), filterSection (isFilter)
{
    setTitle (filterSection ? "Filter power" : "EQ power");
    setTooltip ((filterSection ? "Filter power" : "EQ power")
                 + juce::String (": left-click to toggle, right-click for reset and presets"));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

EQLamp::~EQLamp() = default;

void EQLamp::paint (juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    juce::Path bulb;
    bulb.addEllipse (bounds);
    const float pulse = 0.5f + 0.5f * std::sin (phase);
    const float intensity = enabled ? 0.4f + hoverGlow * (0.2f + pulse * 0.25f) : hoverGlow * 0.18f;
    EmeraldLookAndFeel::drawGlow (graphics, bulb, emerald::accent, intensity);
    graphics.setColour (enabled ? emerald::accent.interpolatedWith (emerald::accentBright, hoverGlow * pulse * 0.3f)
                                : emerald::accentDark.withAlpha (0.4f));
    graphics.fillPath (bulb);
    graphics.setColour (emerald::accent.withAlpha (enabled ? 0.8f : 0.25f));
    graphics.strokePath (bulb, juce::PathStrokeType (1.0f));
    if (enabled)
    {
        graphics.setColour (emerald::accentBright.withAlpha (0.55f));
        graphics.fillEllipse (bounds.withSizeKeepingCentre (3.0f, 3.0f));
    }
}

void EQLamp::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isRightButtonDown())
        showMenu();
    else if (event.mods.isLeftButtonDown())
    {
        auto* parameter = state.getParameter (filterSection ? params::filterEnabled : params::eqEnabled);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
        parameter->endChangeGesture();
    }
    timerCallback();
}

void EQLamp::mouseEnter (const juce::MouseEvent&) { repaint(); }
void EQLamp::mouseExit (const juce::MouseEvent&) { repaint(); }

void EQLamp::showMenu()
{
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;
    menu = std::make_unique<EQContextMenu> (state, *this, filterSection);
    parent->addAndMakeVisible (*menu);
    menu->setTopLeftPosition (juce::jlimit (0, juce::jmax (0, parent->getWidth() - menu->getWidth()), getX()),
                             juce::jmin (getBottom(), parent->getHeight() - menu->getHeight()));
    menu->toFront (false);
}

void EQLamp::timerCallback()
{
    phase += 0.12f;
    hoverGlow += ((isMouseOver() ? 1.0f : 0.0f) - hoverGlow) * 0.2f;
    enabled = state.getRawParameterValue (filterSection ? params::filterEnabled : params::eqEnabled)->load() >= 0.5f;
    if (menu != nullptr && ! menu->isVisible())
        menu.reset();
    repaint();
}

ElectricLogo::ElectricLogo()
{
    setTitle ("DRUM/BREAKER");
    credit.setFont (juce::Font (juce::FontOptions ("Bahnschrift", 9.0f, juce::Font::plain)), false);
    credit.setColour (juce::HyperlinkButton::textColourId, emerald::textDim);
    credit.setTooltip ("hiddenaudiofx.com");
    credit.addMouseListener (this, false);
    addAndMakeVisible (credit);
    startTimerHz (30);
}

void ElectricLogo::resized()
{
    credit.setBounds (getLocalBounds().removeFromBottom (13).withSizeKeepingCentre (104, 13));

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (juce::Font (juce::FontOptions ("Consolas", 25.0f,
                                                       juce::Font::bold | juce::Font::italic)),
                          "DRUM/BREAKER", 0.0f, 0.0f);
    lettering.clear();
    glyphs.createPath (lettering);
    const auto transform = lettering.getTransformToScaleToFit (10.0f, 6.0f,
                                                               (float) getWidth() - 20.0f,
                                                               (float) getHeight() - 20.0f, true);
    lettering.applyTransform (transform);
    letterShapes.clear();

    for (int index = 0; index < glyphs.getNumGlyphs(); ++index)
    {
        juce::Path shape;
        glyphs.getGlyph (index).createPath (shape);
        shape.applyTransform (transform);

        if (index == 4)
        {
            const auto bounds = shape.getBounds();
            shape.clear();
            shape.startNewSubPath (bounds.getRight(), bounds.getY());
            shape.lineTo (bounds.getX() + bounds.getWidth() * 0.28f,
                          bounds.getY() + bounds.getHeight() * 0.56f);
            shape.lineTo (bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.56f);
            shape.lineTo (bounds.getX(), bounds.getBottom());
            shape.lineTo (bounds.getRight() - bounds.getWidth() * 0.18f,
                          bounds.getY() + bounds.getHeight() * 0.40f);
            shape.lineTo (bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.40f);
            shape.closeSubPath();
        }

        letterShapes.push_back (std::move (shape));
    }

    lettering.clear();
    for (const auto& shape : letterShapes)
        lettering.addPath (shape);
}

void ElectricLogo::paint (juce::Graphics& graphics)
{
    const float pulse = 0.5f + 0.5f * std::sin (phase);
    const float energy = 0.12f + pulse * 0.07f + hoverGlow * 0.25f + surge * 0.55f;
    const float progress = breakupStarted < 0.0 ? 1.0f
        : juce::jlimit (0.0f, 1.0f, (float) ((juce::Time::getMillisecondCounterHiRes() - breakupStarted) / 1100.0));
    const bool breaking = progress < 1.0f;

    for (size_t index = 0; index < letterShapes.size(); ++index)
    {
        const auto& shape = letterShapes[index];
        const float delay = (float) index / (float) juce::jmax ((size_t) 1, letterShapes.size() - 1);
        const float localProgress = juce::jlimit (0.0f, 1.0f, progress * 1.6f - delay * 0.6f);
        const float fracture = breaking ? std::sin (localProgress * juce::MathConstants<float>::pi) : 0.0f;
        const auto bounds = shape.getBounds().expanded (4.0f);

        for (int half = 0; half < 2; ++half)
        {
            const float direction = half == 0 ? -1.0f : 1.0f;
            graphics.saveState();
            graphics.addTransform (juce::AffineTransform::translation (
                direction * fracture * (index % 2 == 0 ? 4.0f : -4.0f), direction * fracture * 2.5f));
            const float stencilGap = index == 4 ? 0.0f : 0.45f;
            const auto clip = half == 0 ? bounds.withBottom (shape.getBounds().getCentreY() - stencilGap)
                                        : bounds.withTop (shape.getBounds().getCentreY() + stencilGap);
            juce::Path clipShape;
            clipShape.addRectangle (clip);
            graphics.reduceClipRegion (clipShape);
            EmeraldLookAndFeel::drawGlow (graphics, shape, emerald::accent, energy + fracture * 0.5f);
            graphics.setColour (emerald::accent.interpolatedWith (emerald::accentBright,
                juce::jlimit (0.0f, 1.0f, hoverGlow * 0.25f + surge * 0.5f + fracture * 0.5f)));
            graphics.fillPath (shape);
            graphics.restoreState();
        }
    }

    if (breaking)
    {
        const auto bounds = lettering.getBounds();
        const float front = bounds.getX() + juce::jmin (1.0f, progress * 1.6f) * bounds.getWidth();
        juce::Path arc;
        arc.startNewSubPath (juce::jmax (bounds.getX(), front - 64.0f), bounds.getCentreY());

        for (int segment = 1; segment <= 12; ++segment)
        {
            const float position = juce::jmax (bounds.getX(), front - 64.0f + segment * (64.0f / 12.0f));
            arc.lineTo (position, bounds.getCentreY() + std::sin (segment * 2.7f + phase * 12.0f) * 4.0f);
        }

        EmeraldLookAndFeel::drawGlow (graphics, arc, emerald::accent, (1.0f - progress) * 1.5f);
        graphics.setColour (emerald::accentBright.withAlpha (1.0f - progress));
        graphics.strokePath (arc, juce::PathStrokeType (1.2f));
    }

    if (surge > 0.02f || hoverGlow > 0.05f)
    {
        const auto bounds = lettering.getBounds();
        const float travel = std::fmod (phase * 0.4f, 1.0f);
        const float sparkX = bounds.getX() + travel * bounds.getWidth();
        juce::Path spark;
        spark.startNewSubPath (sparkX - 5.0f, bounds.getY());
        spark.lineTo (sparkX + 2.0f, bounds.getCentreY() - 2.0f);
        spark.lineTo (sparkX - 2.0f, bounds.getCentreY() + 2.0f);
        spark.lineTo (sparkX + 5.0f, bounds.getBottom());
        graphics.saveState();
        graphics.reduceClipRegion (lettering);
        graphics.setColour (emerald::accentBright.withAlpha (surge * 0.8f + hoverGlow * 0.2f));
        graphics.strokePath (spark, juce::PathStrokeType (2.0f));
        graphics.restoreState();
    }

    const float linkGlow = credit.isMouseOverOrDragging() ? 0.7f : 0.0f;
    if (linkGlow > 0.0f || surge > 0.02f)
    {
        const auto bounds = credit.getBounds().toFloat().reduced (9.0f, 3.0f);
        graphics.setColour (emerald::accent.withAlpha ((linkGlow + surge * 0.2f) * 0.25f));
        graphics.drawLine (bounds.getX(), bounds.getBottom(), bounds.getRight(), bounds.getBottom(), 1.0f);
    }
}

void ElectricLogo::mouseEnter (const juce::MouseEvent&)
{
    surge = juce::jmax (surge, 0.45f);
}

void ElectricLogo::mouseExit (const juce::MouseEvent&)
{
    repaint();
}

void ElectricLogo::mouseDown (const juce::MouseEvent& event)
{
    surge = event.mods.isRightButtonDown() ? 1.0f : 0.85f;
    if (event.originalComponent != &credit && lettering.getBounds().contains (event.position))
    {
        breakupStarted = juce::Time::getMillisecondCounterHiRes();
        if (event.mods.isLeftButtonDown() && onReset != nullptr)
            onReset();
        else if (event.mods.isRightButtonDown())
        {
            juce::PopupMenu menu;
            menu.setLookAndFeel (&getLookAndFeel());
            menu.addItem (1, "Restore to default...");
            const juce::Component::SafePointer<ElectricLogo> safeThis (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                .withDeletionCheck (*this), [safeThis] (int result)
            {
                if (result == 1 && safeThis != nullptr)
                {
                    safeThis->surge = 1.0f;
                    safeThis->breakupStarted = juce::Time::getMillisecondCounterHiRes();
                    if (safeThis->onReset != nullptr)
                        safeThis->onReset();
                    safeThis->repaint();
                }
            });
        }
    }
    repaint();
}

void ElectricLogo::timerCallback()
{
    phase += 0.045f;
    surge *= 0.9f;
    const float target = isMouseOver (true) ? 1.0f : 0.0f;
    hoverGlow += (target - hoverGlow) * 0.18f;
    credit.setColour (juce::HyperlinkButton::textColourId,
                      credit.isMouseOverOrDragging() ? emerald::accentBright
                                                    : emerald::textDim.interpolatedWith (emerald::accent, surge));
    repaint();
}

Knob::Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
            const juce::String& captionToUse, const juce::String& tooltip)
    : caption (captionToUse)
{
    slider.setTooltip (captionToUse + ": " + tooltip);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedBottom (13));
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (slider.isMouseOverOrDragging() ? emerald::accentBright : emerald::textDim);
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.drawText (caption, getLocalBounds().removeFromBottom (13), juce::Justification::centred, false);
}

WheelControl::WheelControl (const juce::String& name, const juce::String& tooltip, bool bipolar)
    : springsToCentre (bipolar)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    addAndMakeVisible (label);

    slider.setRange (bipolar ? -2.0 : 0.0, bipolar ? 2.0 : 1.0, 0.01);
    slider.setValue (0.0, juce::dontSendNotification);
    slider.setDoubleClickReturnValue (true, 0.0);
    slider.setTooltip (tooltip);
    slider.onValueChange = [this]
    {
        if (onValueChanged != nullptr)
            onValueChanged ((float) slider.getValue());
    };
    slider.onDragEnd = [this]
    {
        if (springsToCentre)
            slider.setValue (0.0, juce::sendNotificationSync);
    };
    addAndMakeVisible (slider);
}

void WheelControl::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromBottom (14));
    slider.setBounds (area);
}

void WheelControl::setValueFromController (float value)
{
    if (! slider.isMouseButtonDown())
        slider.setValue (value, juce::dontSendNotification);
}

//==============================================================================
DrumBreakerEditor::DrumBreakerEditor (DrumBreakerProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      modPad (p.apvts, params::filterCutoff, params::filterReso),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
    pitchWheel ("PITCH", "Pitch bend: mouse drag or MIDI pitch wheel, +/- 2 semitones", true),
    modWheel ("MOD", "Filter modulation: mouse drag or MIDI CC 1", false),
    reverbKnob     (p.apvts, params::reverb,       "REVERB", "Add reverb to the loaded sample"),
      pitchKnob       (p.apvts, params::pitch,        "PITCH",  "Playback pitch in semitones (centred = original pitch)"),
      sensitivityKnob (p.apvts, params::sensitivity,  "SENSE",  "How eagerly transients are detected when breaking"),
      outputKnob      (p.apvts, params::outputGain,   "OUT",    "Output level"),
      driveKnob       (p.apvts, params::filterDrive,  "DRIVE",  "Saturation into the filter"),
      mixKnob         (p.apvts, params::filterMix,    "MIX",    "Dry / filtered balance")
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (900, 600, 1800, 1200);
    setSize (1060, 680);

    statusLabel.setFont (juce::Font (juce::FontOptions (11.5f)));
    statusLabel.setColour (juce::Label::textColourId, emerald::textDim);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (logo);
    logo.onReset = [this]
    {
        playButtonHeld = false;
        processor.resetToDefaults();
        refreshFromProcessor();
        waveform.setPlayhead (0.0f);
        pitchWheel.setValueFromController (0.0f);
        modWheel.setValueFromController (0.0f);
    };

    hintLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    hintLabel.setColour (juce::Label::textColourId, emerald::accent);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (waveform);
    waveform.onArrangementChanged = [this] (const std::vector<int>& order)
    {
        processor.setArrangement (order);
    };
    waveform.onSelected = [this]
    {
        processor.selectCustomBreak (false);
        waveform.setSelected (true);
        customWaveform.setSelected (false);
    };
    waveform.onFragmentDropped = [this] (BreakData::Ptr source, int sliceIndex, juce::Point<int> pointInTop)
    {
        auto* top = getTopLevelComponent();
        if (top == nullptr || ! customWaveform.isVisible())
            return false;

        const auto pointInEditor = getLocalPoint (top, pointInTop);
        if (! customWaveform.getBounds().contains (pointInEditor))
            return false;

        const auto pointInCustom = customWaveform.getLocalPoint (this, pointInEditor).toFloat();
        if (! customWaveform.acceptsFragmentDropAt (pointInCustom))
            return false;

        customWaveform.acceptFragmentDrop (std::move (source), sliceIndex, pointInCustom.x);
        layoutCustomWaveform();
        processor.selectCustomBreak (true);
        waveform.setSelected (false);
        customWaveform.setSelected (true);
        return true;
    };
    waveform.onFragmentDragged = [this] (BreakData::Ptr source, int sliceIndex, juce::Point<int> pointInTop)
    {
        const auto clearIncoming = [this]
        {
            customWaveform.clearIncomingFragment();
            layoutCustomWaveform();
            return false;
        };

        if (source == nullptr)
            return clearIncoming();

        auto* top = getTopLevelComponent();
        if (top == nullptr || ! customWaveform.isVisible())
            return clearIncoming();

        const auto pointInEditor = getLocalPoint (top, pointInTop);
        if (! customWaveform.getBounds().contains (pointInEditor))
            return clearIncoming();

        auto pointInCustom = customWaveform.getLocalPoint (this, pointInEditor).toFloat();
        if (! customWaveform.acceptsFragmentDropAt (pointInCustom))
            return clearIncoming();

        customWaveform.setIncomingFragment (source, sliceIndex, pointInCustom.x);
        // The display grows to make room for the hovering fragment; re-resolve the
        // insertion point against the resized bounds so the ghost stays under the mouse.
        const auto previousBounds = customWaveform.getBounds();
        layoutCustomWaveform();
        if (customWaveform.getBounds() != previousBounds)
        {
            pointInCustom = customWaveform.getLocalPoint (this, pointInEditor).toFloat();
            customWaveform.setIncomingFragment (std::move (source), sliceIndex, pointInCustom.x);
        }
        return true;
    };

    customWaveform.setCompactMode (true);
    customWaveform.setStatusMessage ("Drag fragments from the waveform above");
    customWaveform.setVisible (false);
    addAndMakeVisible (customWaveform);
    customWaveform.onArrangementChanged = [this] (const std::vector<int>& order)
    {
        processor.setCustomArrangement (order);
        layoutCustomWaveform();
    };
    customWaveform.onSelected = [this]
    {
        processor.selectCustomBreak (true);
        waveform.setSelected (false);
        customWaveform.setSelected (true);
    };

    addAndMakeVisible (spectrum);
    addAndMakeVisible (eqLamp);
    addAndMakeVisible (filterLamp);
    spectrum.setTooltip ("Seven-band EQ: drag frequency/gain, scroll for width, double-click to reset a band");

    addAndMakeVisible (modPad);
    modPad.onCentre = [this]
    {
        auto* motion = processor.apvts.getParameter (params::filterMotion);
        motion->beginChangeGesture();
        motion->setValueNotifyingHost (0.0f);
        motion->endChangeGesture();
        processor.setModWheelFromUI (0.0f);
        modWheel.setValueFromController (0.0f);
    };
    modPad.setTooltip ("X = cutoff, Y = resonance. Center (0, 0) bypasses the filter; double-click the handle to recenter");
    modPad.setAxisNames ("CUTOFF", "RESO");

    addAndMakeVisible (keyboard);
    keyboard.setTooltip ("FL-style typing: Q = C5 (middle C), Z = C4, S = C#4. Hold to loop; release to stop");
    keyboard.setAvailableRange (36, 84);
    keyboard.setLowestVisibleKey (36);
    keyboard.setOctaveForMiddleC (5);

    pitchWheel.onValueChanged = [this] (float value) { processor.setPitchWheelFromUI (value); };
    modWheel.onValueChanged = [this] (float value) { processor.setModWheelFromUI (value); };
    addAndMakeVisible (pitchWheel);
    addAndMakeVisible (modWheel);

    loadButton.setTooltip ("Load a drum loop or one-shot (max 60 seconds)");
    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

    activationButton.setTooltip ("Open DrumBreaker license activation");
    activationButton.onClick = [this] { activationPanel.showActivation(); };
    addAndMakeVisible (activationButton);
    activationPanel.onDismiss = [this]
    {
        tryStartLogoIntro();
        if (! logoIntro.isVisible())
            keyboard.grabKeyboardFocus();
    };
    activationPanel.onActivated = [this] { logoIntroWithSound = true; };
    addChildComponent (activationPanel);
    activationPanel.setBounds (getLocalBounds());
    addChildComponent (logoIntro);
    logoIntro.onFinished = [this] (const juce::String& error)
    {
        if (error.isNotEmpty())
            statusLabel.setText ("Logo intro: " + error, juce::dontSendNotification);
        keyboard.setEnabled (processor.isLicensed());
        if (isShowing() && ! activationPanel.isVisible())
            keyboard.grabKeyboardFocus();
    };

    breakButton.setTooltip ("Break the sample into fragments at every detected transient");
    breakButton.onClick = [this]
    {
        processor.breakSample();
        refreshFromProcessor();
    };
    addAndMakeVisible (breakButton);

    restoreButton.setTooltip ("Restore the unbroken sample");
    restoreButton.onClick = [this]
    {
        processor.restoreWholeSample();
        refreshFromProcessor();
    };
    addAndMakeVisible (restoreButton);

    newBreakButton.setTooltip ("Create a second arrangement by dragging fragments from the original waveform");
    newBreakButton.onClick = [this]
    {
        if (! processor.createCustomBreak())
            return;
        refreshFromProcessor();
        customWaveform.setVisible (true);
        customWaveform.setSelected (true);
        waveform.setSelected (false);
        resized();
    };
    addAndMakeVisible (newBreakButton);

    playButton.setTooltip ("Hold to loop the selected arrangement at C5 pitch; release to stop");
    playButton.setTriggeredOnMouseDown (true);
    playButton.onStateChange = [this]
    {
        const bool down = playButton.isDown();

        if (down != playButtonHeld)
        {
            playButtonHeld = down;

            if (down)
                processor.triggerPreviewOn (previewNote);
            else
                processor.triggerPreviewOff (previewNote);
        }
    };
    addAndMakeVisible (playButton);

    exportButton.setTooltip ("Render the full C5 arrangement with current effects, then save or drag the WAV to a DAW timeline");
    exportButton.onClick = [this] { showExportMenu(); };
    addAndMakeVisible (exportButton);

    presetBox.setTooltip ("Genre filter presets");
    presetBox.addItemList (getFilterPresetNames(), 1);
    addAndMakeVisible (presetBox);
    presetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, params::filterPreset, presetBox);

    filterTypeBox.setTooltip ("Automated filter preset: one low-to-high-to-low sweep per selected arrangement");
    filterTypeBox.addItemList ({ "Select preset...", "Low Pass", "Band Pass", "High Pass" }, 1);
    addAndMakeVisible (filterTypeBox);
    filterMotionAttachment = std::make_unique<juce::ParameterAttachment> (
        *processor.apvts.getParameter (params::filterMotion), [this] (float value)
        {
            const auto motion = (ToneStack::FilterMotion) (int) value;
            const int item = motion == ToneStack::FilterMotion::lowPassSweep ? 2
                : motion == ToneStack::FilterMotion::bandPassSweep ? 3
                : motion == ToneStack::FilterMotion::highPassSweep ? 4 : 1;
            filterTypeBox.setSelectedId (item, juce::dontSendNotification);
        }, nullptr);
    filterTypeBox.onChange = [this]
    {
        const auto motion = filterMotionForPreset (filterTypeBox.getSelectedId() - 1);
        if (motion != ToneStack::FilterMotion::none)
        {
            auto* mix = processor.apvts.getParameter (params::filterMix);
            mix->beginChangeGesture();
            mix->setValueNotifyingHost (1.0f);
            mix->endChangeGesture();
        }
        filterMotionAttachment->setValueAsCompleteGesture ((float) motion);
    };
    filterMotionAttachment->sendInitialUpdate();

    for (auto* k : { &pitchKnob, &sensitivityKnob, &outputKnob,
                     &driveKnob, &mixKnob, &reverbKnob })
        addAndMakeVisible (*k);

    hints.setHostProcessor (&processor);
    hints.addChangeListener (this);
    hints.attachTo (*this);

    processor.addChangeListener (this);
    refreshFromProcessor();

    startTimerHz (30);
    const juce::Component::SafePointer<DrumBreakerEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr && safeThis->isShowing())
        {
            safeThis->activationPanel.showIfUnverified();
            safeThis->tryStartLogoIntro();
            if (! safeThis->activationPanel.isVisible() && ! safeThis->logoIntro.isVisible())
                safeThis->keyboard.grabKeyboardFocus();
        }
    });
}

DrumBreakerEditor::~DrumBreakerEditor()
{
    stopTimer();
    logoIntro.dismiss();
    keyboard.focusLost (juce::Component::focusChangedDirectly);
    exportWorker.removeAllJobs (true, -1);
    processor.removeChangeListener (this);
    hints.removeChangeListener (this);
    hints.detachFrom (*this);
    setLookAndFeel (nullptr);
}

bool DrumBreakerEditor::keyPressed (const juce::KeyPress& key)
{
    if (! processor.isLicensed() || activationPanel.isVisible() || logoIntro.isVisible()
        || dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;
    return keyboard.keyPressed (key);
}

bool DrumBreakerEditor::keyStateChanged (bool isKeyDown)
{
    if (! processor.isLicensed() || activationPanel.isVisible() || logoIntro.isVisible()
        || dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
    {
        keyboard.focusLost (juce::Component::focusChangedDirectly);
        return false;
    }
    return keyboard.keyStateChanged (isKeyDown);
}

void DrumBreakerEditor::visibilityChanged()
{
    if (! isShowing())
    {
        logoIntro.dismiss();
        logoIntroAttempted = false;
    }
}

//==============================================================================
void DrumBreakerEditor::refreshFromProcessor()
{
    auto d = processor.getBreakData();
    auto custom = processor.getCustomBreakData();

    waveform.setData (d);
    waveform.setBpm (processor.getDetectedBpm());
    waveform.setSelected (! processor.isCustomBreakSelected());
    customWaveform.setData (custom);
    customWaveform.setBpm (processor.getDetectedBpm());
    customWaveform.setVisible (custom != nullptr);
    customWaveform.setSelected (processor.isCustomBreakSelected());
    newBreakButton.setEnabled (d != nullptr && processor.hasBrokenSample() && custom == nullptr);
    resized();

    if (d != nullptr)
        statusLabel.setText (d->name + "  |  " + juce::String ((int) d->arrangement.size()) + " fragments",
                             juce::dontSendNotification);
    else
        statusLabel.setText ("No sample loaded", juce::dontSendNotification);
}

void DrumBreakerEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &hints)
    {
        const auto hint = hints.getHint();
        hintLabel.setText (hint.isNotEmpty() ? hint : juce::String ("Drag a drum break onto the waveform to begin"),
                           juce::dontSendNotification);
        return;
    }

    refreshFromProcessor();
}

void DrumBreakerEditor::tryStartLogoIntro()
{
    if (logoIntroAttempted || processor.hasPlayedLogoIntro() || ! isShowing() || ! processor.isLicensed()
        || activationPanel.isVisible())
        return;
    logoIntroAttempted = true;
    processor.markLogoIntroPlayed();
    keyboard.focusLost (juce::Component::focusChangedDirectly);
    const auto result = logoIntro.start (logoIntroWithSound);
    logoIntroWithSound = false;
    if (result.failed())
    {
        statusLabel.setText ("Logo intro: " + result.getErrorMessage(), juce::dontSendNotification);
        keyboard.grabKeyboardFocus();
    }
}

void DrumBreakerEditor::timerCallback()
{
    if (! isShowing())
    {
        logoIntro.dismiss();
        logoIntroAttempted = false;
    }
    const bool licensed = processor.isLicensed();
    if (! licensed && logoIntro.isVisible())
        logoIntro.dismiss();
    tryStartLogoIntro();
    activationButton.setLicenseVerified (licensed);
    playButton.setEnabled (licensed);
    keyboard.setEnabled (licensed && ! logoIntro.isVisible());
    if (! licensed || ! hasKeyboardFocus (true) || activationPanel.isVisible() || logoIntro.isVisible()
        || dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        keyboard.focusLost (juce::Component::focusChangedDirectly);
    if (! licensed && playButtonHeld)
    {
        playButtonHeld = false;
        processor.triggerPreviewOff (previewNote);
    }
    const auto exportData = processor.isCustomBreakSelected() ? processor.getCustomBreakData()
                                                               : processor.getBreakData();
    layoutCustomWaveform();
    exportButton.setEnabled (licensed && ! exporting && exportData != nullptr && exportData->audio != nullptr
                            && exportData->totalLength > 0);
    if (exportMenu != nullptr && ! exportMenu->isVisible())
        exportMenu.reset();
    if (processor.isCustomBreakSelected())
    {
        waveform.setPlayhead (0.0f);
        customWaveform.setPlayhead (processor.getPlayheadNormalised());
    }
    else
    {
        waveform.setPlayhead (processor.getPlayheadNormalised());
        customWaveform.setPlayhead (0.0f);
    }
    pitchWheel.setValueFromController (processor.getPitchWheelSemitones());
    modWheel.setValueFromController (processor.getModWheelAmount());
    const bool filterEnabled = processor.apvts.getRawParameterValue (params::filterEnabled)->load() >= 0.5f;
    const int filterMotion = (int) processor.apvts.getRawParameterValue (params::filterMotion)->load();
    modPad.setLiveFilterValues (processor.getLiveFilterCutoff(), processor.getLiveFilterResonance(), filterEnabled,
                               filterMotion != 0 || processor.getModWheelAmount() > 0.0f);
    filterTypeBox.setEnabled (filterEnabled);
    driveKnob.setEnabled (filterEnabled);
    mixKnob.setEnabled (filterEnabled);
    driveKnob.setAlpha (filterEnabled ? 1.0f : 0.35f);
    mixKnob.setAlpha (filterEnabled ? 1.0f : 0.35f);
    modPad.setAlpha (filterEnabled ? 1.0f : 0.35f);
}

void DrumBreakerEditor::showExportMenu()
{
    if (! processor.isLicensed())
    {
        activationPanel.showActivation();
        return;
    }
    const auto activeData = processor.isCustomBreakSelected() ? processor.getCustomBreakData()
                                                               : processor.getBreakData();
    if (exporting || activeData == nullptr || activeData->totalLength <= 0)
        return;
    const juce::Component::SafePointer<DrumBreakerEditor> safeThis (this);
    exportMenu = std::make_unique<ExportMenu> (exportButton,
        [safeThis] { if (safeThis != nullptr) safeThis->exportToPlaylist(); },
        [safeThis] { if (safeThis != nullptr) safeThis->exportToFolder(); });
    addAndMakeVisible (*exportMenu);
    exportMenu->setTopLeftPosition (juce::jlimit (0, getWidth() - exportMenu->getWidth(), exportButton.getX()),
                                   exportButton.getBottom());
    exportMenu->toFront (false);
}

void DrumBreakerEditor::exportToFolder()
{
    if (! processor.isLicensed())
    {
        activationPanel.showActivation();
        return;
    }
    auto snapshot = processor.captureExportSnapshot();
    if (snapshot.data == nullptr || exporting)
        return;
    fileChooser = std::make_unique<juce::FileChooser> ("Export processed WAV to folder",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    const juce::Component::SafePointer<DrumBreakerEditor> safeThis (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis, snapshot] (const juce::FileChooser& chooser)
        {
            const auto directory = chooser.getResult();
            if (safeThis == nullptr || ! directory.isDirectory())
                return;
            const auto base = directory.getChildFile (juce::File::createLegalFileName (snapshot.data->name))
                .getFileNameWithoutExtension() + " - Drum Breaker";
            safeThis->startExport (snapshot, directory.getNonexistentChildFile (base, ".wav", false), false);
        });
}

void DrumBreakerEditor::exportToPlaylist()
{
    if (! processor.isLicensed())
    {
        activationPanel.showActivation();
        return;
    }
    auto snapshot = processor.captureExportSnapshot();
    if (snapshot.data == nullptr || exporting)
        return;
    const auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Hidden Audio").getChildFile ("Drum Breaker").getChildFile ("Exports");
    const auto created = directory.createDirectory();
    if (created.failed())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed", created.getErrorMessage());
        return;
    }
    const auto base = directory.getChildFile (juce::File::createLegalFileName (snapshot.data->name))
        .getFileNameWithoutExtension() + " - Drum Breaker";
    startExport (snapshot, directory.getNonexistentChildFile (base, ".wav", false), true);
}

void DrumBreakerEditor::startExport (OfflineExporter::Snapshot snapshot, const juce::File& destination, bool playlist)
{
    if (! snapshot.license.isVerified())
    {
        activationPanel.showActivation();
        return;
    }
    if (exporting)
        return;
    exporting = true;
    exportButton.setEnabled (false);
    statusLabel.setText ("Rendering WAV...", juce::dontSendNotification);
    renderedFileButton.reset();
    const juce::Component::SafePointer<DrumBreakerEditor> safeThis (this);
    exportWorker.addJob (new ExportJob (std::move (snapshot), destination,
        [safeThis, destination, playlist] (juce::Result result)
        {
            if (safeThis == nullptr)
                return;
            safeThis->exporting = false;
            safeThis->statusLabel.setText (result.wasOk() ? "Exported: " + destination.getFileName()
                                                         : "Export failed", juce::dontSendNotification);
            if (result.failed())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    "Export failed", result.getErrorMessage());
                return;
            }
            if (playlist)
            {
                safeThis->renderedFileButton = std::make_unique<RenderedFileButton> (destination);
                safeThis->addAndMakeVisible (*safeThis->renderedFileButton);
                safeThis->renderedFileButton->setBounds (
                    juce::jlimit (0, safeThis->getWidth() - 320, safeThis->exportButton.getX()),
                    safeThis->exportButton.getBottom(), 320, 40);
                safeThis->renderedFileButton->toFront (false);
            }
            else
                destination.revealToUser();
        }), true);
}

//==============================================================================
void DrumBreakerEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load a drum break",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  if (file.existsAsFile())
                                      loadFile (file);
                              });
}

void DrumBreakerEditor::loadFile (const juce::File& file)
{
    juce::String error;

    if (processor.loadSampleFile (file, error))
    {
        refreshFromProcessor();
        hintLabel.setText ("Loaded " + file.getFileName(), juce::dontSendNotification);
    }
    else
    {
        waveform.setStatusMessage (error);
        hintLabel.setText (error, juce::dontSendNotification);
    }
}

bool DrumBreakerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (logoIntro.isVisible())
        return false;
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;

    return false;
}

void DrumBreakerEditor::fileDragEnter (const juce::StringArray&, int x, int y)
{
    waveform.setDragHighlight (! customWaveform.isVisible() || ! customWaveform.getBounds().contains (x, y));
}

void DrumBreakerEditor::fileDragExit (const juce::StringArray&)
{
    waveform.setDragHighlight (false);
}

void DrumBreakerEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    waveform.setDragHighlight (false);

    if (customWaveform.isVisible() && customWaveform.getBounds().contains (x, y))
        return;

    for (const auto& f : files)
    {
        const juce::File file (f);

        if (file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
        {
            loadFile (file);
            break;
        }
    }
}

//==============================================================================
void DrumBreakerEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title) const
{
    if (area.isEmpty())
        return;

    auto bounds = area.toFloat();

    g.setColour (emerald::panel.withAlpha (0.55f));
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (emerald::accent.withAlpha (0.22f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (emerald::textDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText (title, area.reduced (10, 5).removeFromTop (12), juce::Justification::topLeft, false);
}

void DrumBreakerEditor::paint (juce::Graphics& g)
{
    g.fillAll (emerald::background);

    // subtle emerald vignette
    g.setGradientFill (juce::ColourGradient (emerald::accentDark.withAlpha (0.18f),
                                             (float) getWidth() * 0.5f, 0.0f,
                                             emerald::background.withAlpha (0.0f),
                                             (float) getWidth() * 0.5f, (float) getHeight() * 0.7f, false));
    g.fillRect (getLocalBounds());

    drawPanel (g, controlPanel, "TRANSPORT");
    drawPanel (g, eqPanel,      "EQ / ANALYSER");
    drawPanel (g, filterPanel,  "FILTER / MODULATION");

    g.setColour (emerald::panel.withAlpha (0.8f));
    g.fillRect (getLocalBounds().removeFromBottom (22));
    g.setColour (emerald::accent.withAlpha (0.25f));
    g.drawLine (0.0f, (float) getHeight() - 22.0f, (float) getWidth(), (float) getHeight() - 22.0f, 1.0f);
}

void DrumBreakerEditor::resized()
{
    activationPanel.setBounds (getLocalBounds());
    logoIntro.setBounds (getLocalBounds());
    auto area = getLocalBounds();

    auto footer = area.removeFromBottom (22);
    hintLabel.setBounds (footer.reduced (10, 0));

    auto headerRow = area.removeFromTop (40);
    auto header = headerRow.withHeight (38).reduced (10, 6);
    loadButton.setBounds (header.removeFromLeft (90).reduced (2));
    header.removeFromLeft (6);
    activationButton.setBounds (header.removeFromLeft (100).reduced (2));
    logo.setBounds (headerRow.withTrimmedTop (2).withSizeKeepingCentre (284, 38));
    statusLabel.setBounds (header.withLeft (logo.getRight() + 12).reduced (6, 0));

    // The original visualiser spans the full width but only about a sixth of the window height.
    const int waveformHeight = juce::jmax (110, area.getHeight() / 6);
    waveform.setBounds (area.removeFromTop (waveformHeight).reduced (10, 4));
    if (customWaveform.isVisible())
    {
        area.removeFromTop (2);
        const auto customRow = area.removeFromTop (waveformHeight);
        customWaveform.setBounds (waveform.getX(), customRow.getY(), waveform.getWidth(), waveform.getHeight());
        layoutCustomWaveform();
    }

    auto keyboardArea = area.removeFromBottom (86).reduced (10, 6);
    auto wheelColumn = keyboardArea.removeFromLeft (96);
    auto reverbColumn = keyboardArea.removeFromRight (96);

    pitchWheel.setBounds (wheelColumn.removeFromLeft (wheelColumn.getWidth() / 2));
    modWheel.setBounds (wheelColumn);

    constexpr int whiteKeysFromC3ToC7 = 29;
    auto keyboardBounds = keyboardArea.reduced (4, 0);
    keyboard.setKeyWidth ((float) keyboardBounds.getWidth() / (float) whiteKeysFromC3ToC7);
    keyboard.setBounds (keyboardBounds);
    reverbKnob.setBounds (reverbColumn.withSizeKeepingCentre (82, reverbColumn.getHeight()));

    controlPanel = area.removeFromTop (96).reduced (10, 4);
    {
        auto panel = controlPanel.reduced (12, 16);

        const int knobSize = juce::jmin (64, panel.getHeight());
        auto left = panel.removeFromLeft (knobSize * 2 + 10);
        pitchKnob.setBounds (left.removeFromLeft (knobSize));
        left.removeFromLeft (10);
        sensitivityKnob.setBounds (left.removeFromLeft (knobSize));

        auto right = panel.removeFromRight (knobSize);
        outputKnob.setBounds (right);

        auto centre = panel.withSizeKeepingCentre (juce::jmin (500, panel.getWidth()), 44);
        const int gap = 8;
        const int buttonWidth = (centre.getWidth() - gap * 4) / 5;
        for (auto* button : { &exportButton, &breakButton, &playButton, &restoreButton, &newBreakButton })
        {
            button->setBounds (centre.removeFromLeft (buttonWidth));
            centre.removeFromLeft (gap);
        }
        if (renderedFileButton != nullptr)
            renderedFileButton->setBounds (juce::jlimit (0, getWidth() - 320, exportButton.getX()),
                                           exportButton.getBottom(), 320, 40);
        if (exportMenu != nullptr)
            exportMenu->setTopLeftPosition (juce::jlimit (0, getWidth() - exportMenu->getWidth(), exportButton.getX()),
                                           exportButton.getBottom());
    }

    area.removeFromTop (4);

    auto lower = area.reduced (10, 4);
    eqPanel     = lower.removeFromLeft (lower.getWidth() / 2).reduced (0, 0);
    filterPanel = lower.withTrimmedLeft (10);
    juce::GlyphArrangement eqTitleGlyphs;
    eqTitleGlyphs.addLineOfText (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)),
                                "EQ / ANALYSER", 0.0f, 0.0f);
    eqLamp.setBounds (eqPanel.getX() + 10 + juce::roundToInt (eqTitleGlyphs.getBoundingBox (0, -1, true).getRight()),
                      eqPanel.getY() + 2, 18, 18);
    juce::GlyphArrangement filterTitleGlyphs;
    filterTitleGlyphs.addLineOfText (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)),
                                    "FILTER / MODULATION", 0.0f, 0.0f);
    filterLamp.setBounds (filterPanel.getX() + 10 + juce::roundToInt (filterTitleGlyphs.getBoundingBox (0, -1, true).getRight()),
                          filterPanel.getY() + 2, 18, 18);

    {
        auto panel = eqPanel.reduced (12, 20);
        spectrum.setBounds (panel);
    }

    {
        auto panel = filterPanel.reduced (12, 20);
        auto side = panel.removeFromRight (juce::jmin (90, panel.getWidth() / 3));

        const int knobH = juce::jmin (74, side.getHeight() / 3);
        filterTypeBox.setBounds (side.removeFromTop (26).reduced (0, 2));
        side.removeFromTop (6);
        driveKnob.setBounds (side.removeFromTop (knobH).reduced (6, 0));
        mixKnob.setBounds (side.removeFromTop (knobH).reduced (6, 0));

        panel.removeFromRight (10);
        presetBox.setBounds (panel.removeFromBottom (28));
        panel.removeFromBottom (6);
        modPad.setBounds (panel);
    }
}

void DrumBreakerEditor::layoutCustomWaveform()
{
    if (! customWaveform.isVisible() || waveform.getWidth() <= 0)
        return;

    customWaveform.setFixedPixelsPerSample (waveform.getPixelsPerSample());
    const int maximumWidth = waveform.getWidth();
    const int minimumWidth = juce::jmax (1, maximumWidth / 3);
    auto customData = processor.getCustomBreakData();
    const double length = (customData != nullptr ? (double) customData->totalLength : 0.0)
                        + customWaveform.getIncomingLength();
    const int preferredWidth = customWaveform.getPreferredContentWidth (length, minimumWidth, maximumWidth);
    const int x = waveform.getX() + (maximumWidth - preferredWidth) / 2;
    customWaveform.setBounds (x, waveform.getBottom() + 6, preferredWidth, waveform.getHeight());
}

} // namespace drumbreaker
