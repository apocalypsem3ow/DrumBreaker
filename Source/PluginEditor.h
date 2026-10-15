#pragma once

#include "PluginProcessor.h"
#include "UI/EmeraldLookAndFeel.h"
#include "UI/HintBroadcaster.h"
#include "UI/SpectrumDisplay.h"
#include "UI/WaveformEditor.h"
#include "UI/XYPad.h"
#include "UI/ActivationPanel.h"
#include "UI/LogoIntro.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace drumbreaker
{

class ActivationStatusButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    void setLicenseVerified (bool verified);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool licenseVerified = false;
};

/** Labelled rotary control wired to a parameter. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
          const juce::String& caption, const juce::String& tooltip);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** Vertical pitch/modulation wheel that reflects both mouse and MIDI input. */
class WheelControl : public juce::Component
{
public:
    WheelControl (const juce::String& name, const juce::String& tooltip, bool bipolar);

    void resized() override;
    void setValueFromController (float value);

    std::function<void(float)> onValueChanged;

private:
    juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label label;
    bool springsToCentre = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WheelControl)
};

//==============================================================================
/** MidiKeyboardComponent that can carry a tooltip / hint. */
class PianoRoll : public juce::MidiKeyboardComponent,
                  public juce::SettableTooltipClient
{
public:
    PianoRoll (juce::MidiKeyboardState&, Orientation);
    static int noteForTypingKey (int keyCode) noexcept;
};

//==============================================================================
class ElectricLogo : public juce::Component,
                     private juce::Timer
{
public:
    ElectricLogo();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onReset;

private:
    void timerCallback() override;

    juce::HyperlinkButton credit { "by hidden audio", juce::URL ("https://hiddenaudiofx.com") };
    juce::Path lettering;
    std::vector<juce::Path> letterShapes;
    double breakupStarted = -1.0;
    float phase = 0.0f;
    float surge = 0.0f;
    float hoverGlow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElectricLogo)
};

//==============================================================================
class EQContextMenu;
class ExportMenu;
class RenderedFileButton;

class EQLamp : public juce::Component,
               public juce::SettableTooltipClient,
               private juce::Timer
{
public:
    explicit EQLamp (juce::AudioProcessorValueTreeState&, bool filterSection = false);
    ~EQLamp() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showMenu();

    juce::AudioProcessorValueTreeState& state;
    const bool filterSection;
    std::unique_ptr<EQContextMenu> menu;
    float phase = 0.0f;
    float hoverGlow = 0.0f;
    bool enabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQLamp)
};

//==============================================================================
class DrumBreakerEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    explicit DrumBreakerEditor (DrumBreakerProcessor&);
    ~DrumBreakerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void visibilityChanged() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshFromProcessor();
    void openFileChooser();
    void showExportMenu();
    void exportToFolder();
    void exportToPlaylist();
    void startExport (OfflineExporter::Snapshot snapshot, const juce::File& destination, bool playlist);
    void loadFile (const juce::File&);
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title) const;
    void layoutCustomWaveform();
    void tryStartLogoIntro();

    DrumBreakerProcessor& processor;

    EmeraldLookAndFeel lookAndFeel;
    HintBroadcaster hints;
    juce::TooltipWindow tooltipWindow { this, 450 };

    ElectricLogo logo;
    WaveformEditor waveform;
    WaveformEditor customWaveform;
    SpectrumDisplay spectrum { processor.spectrumTap, processor.apvts };
    EQLamp eqLamp { processor.apvts };
    EQLamp filterLamp { processor.apvts, true };
    XYPad modPad;
    PianoRoll keyboard;
    WheelControl pitchWheel, modWheel;

    juce::TextButton loadButton   { "LOAD" };
    ActivationStatusButton activationButton { "ACTIVATE" };
    juce::TextButton breakButton  { "BREAK" };
    juce::TextButton restoreButton{ "RESTORE" };
    juce::TextButton newBreakButton { "NEW BREAK" };
    juce::TextButton playButton   { "PLAY" };
    juce::TextButton exportButton { "EXPORT TO..." };

    juce::ComboBox presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> presetAttachment;

    juce::ComboBox filterTypeBox;
    std::unique_ptr<juce::ParameterAttachment> filterMotionAttachment;

    Knob reverbKnob, pitchKnob, sensitivityKnob, outputKnob;
    Knob driveKnob, mixKnob;

    juce::Label hintLabel, statusLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<ExportMenu> exportMenu;
    std::unique_ptr<RenderedFileButton> renderedFileButton;
    juce::ThreadPool exportWorker { 1 };
    bool exporting = false;
    ActivationPanel activationPanel { processor.getLicenseClient() };
    LogoIntro logoIntro;
    bool logoIntroAttempted = false;
    bool logoIntroWithSound = false;

    juce::Rectangle<int> eqPanel, filterPanel, controlPanel;
    bool playButtonHeld = false;

    static constexpr int previewNote = 60; // C5 plays at the loaded pitch

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumBreakerEditor)
};

} // namespace drumbreaker
