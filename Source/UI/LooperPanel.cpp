#include "LooperPanel.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

LooperPanel::LooperPanel (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    // Looper controls must fire the instant the mouse goes down — timing IS
    // the feature.
    mainButton.setTriggeredOnMouseDown (true);
    stopButton.setTriggeredOnMouseDown (true);
    clearButton.setTriggeredOnMouseDown (true);

    mainButton.onClick  = [this] { proc.getLooper().tapMain();  };
    stopButton.onClick  = [this] { proc.getLooper().tapStop();  };
    clearButton.onClick = [this] { proc.getLooper().tapClear(); };

    addAndMakeVisible (mainButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (clearButton);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setRange (0.0, 1.5, 0.01);
    volumeSlider.setValue (proc.getLooper().getVolume(), juce::dontSendNotification);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.onValueChange = [this]
    {
        proc.getLooper().setVolume ((float) volumeSlider.getValue());
    };
    addAndMakeVisible (volumeSlider);

    volumeLabel.setText ("LOOP VOL", juce::dontSendNotification);
    volumeLabel.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Semibold")));
    volumeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (volumeLabel);

    startTimerHz (30);
}

LooperPanel::~LooperPanel()
{
    stopTimer();
}

void LooperPanel::timerCallback()
{
    // Keep the main button's label in sync with the looper state.
    switch (proc.getLooper().getState())
    {
        case LoopStation::Empty:     mainButton.setButtonText ("REC");     break;
        case LoopStation::Recording: mainButton.setButtonText ("SET");     break;
        case LoopStation::Playing:   mainButton.setButtonText ("OVERDUB"); break;
        case LoopStation::Overdub:   mainButton.setButtonText ("PLAY");    break;
        case LoopStation::Stopped:   mainButton.setButtonText ("OVERDUB"); break;
        default: break;
    }
    repaint();
}

void LooperPanel::resized()
{
    auto area = getLocalBounds().reduced (24);

    auto bottom = area.removeFromBottom (40);
    volumeLabel.setBounds (bottom.removeFromLeft (90));
    bottom.removeFromLeft (8);
    volumeSlider.setBounds (bottom.removeFromLeft (juce::jmin (260, bottom.getWidth() / 2)));

    auto buttons = area.removeFromBottom (56);
    const int bw = juce::jmin (170, (buttons.getWidth() - 32) / 3);
    auto strip = buttons.withSizeKeepingCentre (bw * 3 + 32, 44);
    mainButton.setBounds  (strip.removeFromLeft (bw));
    strip.removeFromLeft (16);
    stopButton.setBounds  (strip.removeFromLeft (bw));
    strip.removeFromLeft (16);
    clearButton.setBounds (strip.removeFromLeft (bw));
}

void LooperPanel::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    auto card = getLocalBounds().toFloat().reduced (2.0f);
    const float radius = theme.cornerRadius;

    // Opaque glass card — this tab covers the studio controls beneath it.
    g.setColour (theme.glow >= 0.9f ? juce::Colour (0xf0070b1e)
                                    : theme.bgTop);
    g.fillRoundedRectangle (card, radius);
    g.setColour (theme.accent.withAlpha (theme.glow >= 0.9f ? 0.35f : 0.15f));
    g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.2f);

    auto& looper = proc.getLooper();
    const int   st     = looper.getState();
    const float posN   = looper.getPosition();
    const int   layers = looper.getLayers();

    // --- Progress ring ------------------------------------------------------
    const float ringR = juce::jmin (card.getWidth(), card.getHeight()) * 0.24f;
    const juce::Point<float> centre (card.getCentreX(), card.getY() + card.getHeight() * 0.40f);

    g.setColour (theme.controlTrack);
    g.drawEllipse (centre.x - ringR, centre.y - ringR, ringR * 2.0f, ringR * 2.0f, 5.0f);

    const juce::Colour stateColour =
        st == LoopStation::Recording ? juce::Colour (0xffff453a)
      : st == LoopStation::Overdub   ? theme.waveform
      : st == LoopStation::Playing   ? theme.accent
      : theme.textSecondary;

    if (st != LoopStation::Empty)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                           0.0f, juce::MathConstants<float>::twoPi * juce::jmax (0.02f, posN),
                           true);
        if (theme.glow >= 0.9f)
        {
            g.setColour (stateColour.withAlpha (0.25f));
            g.strokePath (arc, juce::PathStrokeType (11.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }
        g.setColour (stateColour);
        g.strokePath (arc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // --- Centre status ------------------------------------------------------
    g.setColour (theme.text);
    g.setFont (juce::Font (juce::FontOptions (26.0f).withStyle ("Semibold")));
    const juce::String stateText =
        st == LoopStation::Empty     ? "READY"
      : st == LoopStation::Recording ? "REC"
      : st == LoopStation::Overdub   ? "DUB"
      : st == LoopStation::Playing   ? "PLAY"
      : "STOP";
    g.drawText (stateText,
                juce::Rectangle<float> (ringR * 2.0f, 34.0f).withCentre (centre),
                juce::Justification::centred);

    g.setColour (theme.textSecondary);
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Medium")));
    g.drawText (juce::String ("LAYERS ") + juce::String (layers),
                juce::Rectangle<float> (ringR * 2.0f, 20.0f)
                    .withCentre ({ centre.x, centre.y + 26.0f }),
                juce::Justification::centred);

    // --- How-to line ---------------------------------------------------------
    g.setColour (theme.textSecondary);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText ("REC: play your first loop   >   SET: lock the length   >   OVERDUB: stack layers   (keys below stay live)",
                card.reduced (16.0f).removeFromBottom (110.0f).removeFromTop (18.0f),
                juce::Justification::centred);
}
