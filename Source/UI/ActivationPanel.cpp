#include "ActivationPanel.h"
#include "EmeraldLookAndFeel.h"

namespace drumbreaker
{

ActivationPanel::ActivationPanel (licensing::LicenseClient& licenseClient) : client (licenseClient)
{
    setTitle ("Drum Breaker license activation");
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    titleLabel.setText ("DRUM/BREAKER", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions ("Consolas", 23.0f, juce::Font::bold | juce::Font::italic)));
    titleLabel.setColour (juce::Label::textColourId, emerald::accentBright);
    subtitleLabel.setText ("License activation", juce::dontSendNotification);
    subtitleLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
    keyLabel.setText ("License key", juce::dontSendNotification);
    keyLabel.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    statusLabel.setFont (juce::Font (juce::FontOptions (11.5f)));
    statusLabel.setColour (juce::Label::textColourId, emerald::textDim);
    statusLabel.setJustificationType (juce::Justification::topLeft);
    for (auto* label : { &titleLabel, &subtitleLabel, &keyLabel, &statusLabel })
    {
        label->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*label);
    }

    keyEditor.setMultiLine (false);
    keyEditor.setReturnKeyStartsNewLine (false);
    keyEditor.setPasswordCharacter (0);
    keyEditor.setInputRestrictions (512);
    keyEditor.setFont (juce::Font (juce::FontOptions ("Consolas", 14.0f, juce::Font::plain)));
    keyEditor.setBorder (juce::BorderSize<int> (1));
    keyEditor.setIndents (6, 0);
    keyEditor.setJustification (juce::Justification::centredLeft);
    keyEditor.setTextToShowWhenEmpty ("License key from your purchase email", emerald::textDim);
    keyEditor.setColour (juce::TextEditor::backgroundColourId, emerald::background);
    keyEditor.setColour (juce::TextEditor::textColourId, emerald::text);
    keyEditor.setColour (juce::TextEditor::outlineColourId, emerald::accentDark);
    keyEditor.setColour (juce::TextEditor::focusedOutlineColourId, emerald::accent);
    keyEditor.setColour (juce::TextEditor::highlightColourId, emerald::accentDark);
    keyEditor.setColour (juce::TextEditor::highlightedTextColourId, emerald::accentBright);
    keyEditor.setColour (juce::CaretComponent::caretColourId, emerald::accentBright);
    keyEditor.onTextChange = [this] { timerCallback(); };
    keyEditor.onReturnKey = [this] { attemptActivation(); };
    keyEditor.onEscapeKey = [this] { dismiss(); };
    activateButton.onClick = [this] { attemptActivation(); };
    activateButton.setEnabled (false);
    activateButton.setTooltip ("Check and activate your DrumBreaker key with Lemon Squeezy; a successful new activation uses one slot");
    dismissButton.onClick = [this] { dismiss(); };
    dismissButton.setTooltip ("Close this panel; playback and export remain locked until verification succeeds");
    addAndMakeVisible (keyEditor);
    addAndMakeVisible (activateButton);
    addAndMakeVisible (dismissButton);
    setVisible (false);
    startTimerHz (15);
}

void ActivationPanel::showActivation()
{
    pendingAutomaticPrompt = false;
    keyEditor.clear();
    activateButton.setEnabled (false);
    timerCallback();
    setVisible (true);
    toFront (false);
    if (! busy && ! verified && keyEditor.isShowing())
        keyEditor.grabKeyboardFocus();
}

void ActivationPanel::showIfUnverified()
{
    pendingAutomaticPrompt = true;
    timerCallback();
}

void ActivationPanel::attemptActivation()
{
    if (busy || verified)
        return;
    if (keyEditor.getText().trim().isEmpty())
    {
        const auto status = client.getStatus();
        if (status.saved && ! status.verified)
        {
            client.validateSaved();
            timerCallback();
            return;
        }
        statusLabel.setColour (juce::Label::textColourId, emerald::warning);
        statusLabel.setText ("Enter a license key first.", juce::dontSendNotification);
        return;
    }
    client.activate (keyEditor.getText());
    timerCallback();
}

void ActivationPanel::timerCallback()
{
    const auto status = client.getStatus();
    busy = status.busy;
    verified = status.verified;
    keyEditor.setEnabled (! busy);
    keyEditor.setReadOnly (verified);
    activateButton.setVisible (! verified);
    const bool retrySaved = status.saved && ! status.verified && keyEditor.getText().trim().isEmpty();
    activateButton.setEnabled (! busy && (keyEditor.getText().trim().isNotEmpty() || retrySaved));
    activateButton.setButtonText (busy ? "CHECKING..." : retrySaved ? "RETRY CHECK" : "ACTIVATE");
    dismissButton.setButtonText (verified ? "Continue" : "Close");
    if (displayedStatus != status.message)
    {
        displayedStatus = status.message;
        statusLabel.setColour (juce::Label::textColourId, busy ? emerald::textDim
                    : verified && status.saved ? emerald::accentBright : emerald::warning);
        statusLabel.setText (displayedStatus, juce::dontSendNotification);
    }
    if (pendingAutomaticPrompt && ! busy)
    {
        pendingAutomaticPrompt = false;
        if (! verified)
            showActivation();
    }
}

void ActivationPanel::dismiss()
{
    pendingAutomaticPrompt = false;
    keyEditor.clear();
    activateButton.setEnabled (false);
    setVisible (false);
    if (onDismiss != nullptr)
        onDismiss();
}

void ActivationPanel::resized()
{
    panelBounds = getLocalBounds().withSizeKeepingCentre (juce::jmin (480, getWidth() - 32), 324);
    auto area = panelBounds.reduced (28, 24);
    titleLabel.setBounds (area.removeFromTop (32));
    subtitleLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (18);
    keyLabel.setBounds (area.removeFromTop (20));
    keyEditor.setBounds (area.removeFromTop (40).reduced (2, 0).withHeight (26));
    area.removeFromTop (10);
    statusLabel.setBounds (area.removeFromTop (70));
    auto buttons = area.removeFromBottom (36);
    dismissButton.setBounds (buttons.removeFromLeft (110));
    activateButton.setBounds (buttons.removeFromRight (130));
}

void ActivationPanel::paint (juce::Graphics& graphics)
{
    graphics.fillAll (emerald::background.withAlpha (0.88f));
    juce::Path frame;
    frame.addRoundedRectangle (panelBounds.toFloat().reduced (1.0f), 6.0f);
    EmeraldLookAndFeel::drawGlow (graphics, frame, emerald::accent, 0.25f);
    graphics.setColour (emerald::panel);
    graphics.fillPath (frame);
    graphics.setColour (emerald::accent.withAlpha (0.55f));
    graphics.strokePath (frame, juce::PathStrokeType (1.0f));
}

} // namespace drumbreaker