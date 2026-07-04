#include "LooperPanel.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

LooperPanel::LooperPanel (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    // Looper controls must fire the instant the mouse goes down — timing IS
    // the feature.
    for (int i = 0; i < LoopStation::kNumTracks; ++i)
    {
        auto& t = trackUI[i];

        t.mainButton.setTriggeredOnMouseDown (true);
        t.undoButton.setTriggeredOnMouseDown (true);
        t.clearButton.setTriggeredOnMouseDown (true);

        t.mainButton.onClick  = [this, i] { proc.getLooper().tapMain (i);  };
        t.undoButton.onClick  = [this, i] { proc.getLooper().tapUndo (i);  };
        t.clearButton.onClick = [this, i] { proc.getLooper().tapClear (i); };

        t.muteButton.setClickingTogglesState (true);
        t.muteButton.onClick = [this, i]
        {
            proc.getLooper().setMuted (i, trackUI[i].muteButton.getToggleState());
        };

        t.volSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        t.volSlider.setRange (0.0, 1.5, 0.01);
        t.volSlider.setValue (proc.getLooper().getTrackVolume (i),
                              juce::dontSendNotification);
        t.volSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        t.volSlider.onValueChange = [this, i]
        {
            proc.getLooper().setTrackVolume (i, (float) trackUI[i].volSlider.getValue());
        };

        addAndMakeVisible (t.mainButton);
        addAndMakeVisible (t.undoButton);
        addAndMakeVisible (t.clearButton);
        addAndMakeVisible (t.muteButton);
        addAndMakeVisible (t.volSlider);
    }

    playAllButton.setTriggeredOnMouseDown (true);
    stopAllButton.setTriggeredOnMouseDown (true);
    playAllButton.onClick  = [this] { proc.getLooper().tapPlayAll();  };
    stopAllButton.onClick  = [this] { proc.getLooper().tapStopAll();  };
    clearAllButton.onClick = [this] { proc.getLooper().tapClearAll(); };
    addAndMakeVisible (playAllButton);
    addAndMakeVisible (stopAllButton);
    addAndMakeVisible (clearAllButton);

    // --- Sound pickers: choose the next layer's engine + instrument here ---
    engineBox.addItem ("Chop", 1);
    engineBox.addItem ("Synth", 2);
    engineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.getAPVTS(), "engine", engineBox);
    addAndMakeVisible (engineBox);

    {
        static const char* featured[] = { "Vox Choir", "Vox Pluck", "Supersaw Lead",
                                          "Rage Bell", "Memphis 808", "Lov3 Keys",
                                          "Kick 808", "Hat Closed", "Snare 808",
                                          "Clap", "Bass Pad", "Syn Grand" };
        const auto names = VocalChopAudioProcessor::getInstrumentNames();
        const auto cats  = VocalChopAudioProcessor::getInstrumentCategories();

        auto* root = instrumentBox.getRootMenu();
        root->addSectionHeader (juce::String::fromUTF8 ("\xe2\x98\x85 QUICK"));
        for (auto* f : featured)
        {
            const int idx = names.indexOf (f);
            if (idx >= 0)
                instrumentBox.addItem (f, 1000 + idx);
        }
        root->addSeparator();

        int i = 0;
        while (i < names.size())
        {
            const juce::String cat = cats[i];
            juce::PopupMenu sub;
            while (i < names.size() && cats[i] == cat)
            {
                sub.addItem (i + 1, names[i]);
                ++i;
            }
            root->addSubMenu (cat, sub);
        }
    }
    instrumentBox.setTextWhenNothingSelected ("Instrument");
    instrumentBox.onChange = [this]
    {
        const int id = instrumentBox.getSelectedId();
        if (id >= 1000)
            proc.applyInstrument (id - 1000);
        else if (id > 0)
            proc.applyInstrument (id - 1);
    };
    addAndMakeVisible (instrumentBox);

    startTimerHz (30);
}

LooperPanel::~LooperPanel()
{
    stopTimer();
}

void LooperPanel::timerCallback()
{
    // Mirror an instrument change made anywhere else.
    {
        const int want = proc.getCurrentInstrument() + 1;
        if (instrumentBox.getSelectedId() != want
            && instrumentBox.getSelectedId() < 1000)
            instrumentBox.setSelectedId (want, juce::dontSendNotification);
    }

    auto& looper = proc.getLooper();
    for (int i = 0; i < LoopStation::kNumTracks; ++i)
    {
        auto& t = trackUI[i];
        switch (looper.getTrackState (i))
        {
            case LoopStation::Empty:     t.mainButton.setButtonText ("REC");     break;
            case LoopStation::Recording: t.mainButton.setButtonText ("SET");     break;
            case LoopStation::Playing:   t.mainButton.setButtonText ("OVERDUB"); break;
            case LoopStation::Overdub:   t.mainButton.setButtonText ("PLAY");    break;
            case LoopStation::Stopped:   t.mainButton.setButtonText ("GO");      break;
            default: break;
        }
        t.undoButton.setEnabled (looper.canUndo (i));
    }
    repaint();
}

void LooperPanel::resized()
{
    auto area = getLocalBounds().reduced (20);

    // Sound pickers along the top of the panel.
    auto pickers = area.removeFromTop (34);
    auto pickStrip = pickers.withSizeKeepingCentre (juce::jmin (440, pickers.getWidth()), 30);
    engineBox.setBounds (pickStrip.removeFromLeft (110));
    pickStrip.removeFromLeft (12);
    instrumentBox.setBounds (pickStrip);

    // Master transport row at the bottom.
    auto master = area.removeFromBottom (44);
    const int mw = juce::jmin (150, (master.getWidth() - 32) / 3);
    auto mStrip = master.withSizeKeepingCentre (mw * 3 + 32, 36);
    playAllButton.setBounds  (mStrip.removeFromLeft (mw));
    mStrip.removeFromLeft (16);
    stopAllButton.setBounds  (mStrip.removeFromLeft (mw));
    mStrip.removeFromLeft (16);
    clearAllButton.setBounds (mStrip.removeFromLeft (mw));

    area.removeFromBottom (26);   // how-to line (painted)
    area.removeFromTop (8);

    // Four track strips side by side.
    const int gap = 14;
    const int stripW = (area.getWidth() - gap * (LoopStation::kNumTracks - 1))
                       / LoopStation::kNumTracks;
    for (int i = 0; i < LoopStation::kNumTracks; ++i)
    {
        auto strip = area.removeFromLeft (stripW);
        if (i < LoopStation::kNumTracks - 1)
            area.removeFromLeft (gap);
        auto& t = trackUI[i];

        auto vol = strip.removeFromBottom (24);
        t.volSlider.setBounds (vol.reduced (6, 0));

        auto small = strip.removeFromBottom (30);
        const int sw = (small.getWidth() - 12) / 3;
        t.undoButton.setBounds  (small.removeFromLeft (sw));
        small.removeFromLeft (6);
        t.muteButton.setBounds  (small.removeFromLeft (sw));
        small.removeFromLeft (6);
        t.clearButton.setBounds (small);

        strip.removeFromBottom (6);
        auto mainB = strip.removeFromBottom (42);
        t.mainButton.setBounds (mainB);

        strip.removeFromBottom (6);
        t.ringArea = strip;   // whatever remains hosts the progress ring
    }
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

    for (int i = 0; i < LoopStation::kNumTracks; ++i)
    {
        const auto& t = trackUI[i];
        if (t.ringArea.isEmpty())
            continue;

        const int   st     = looper.getTrackState (i);
        const float posN   = looper.getTrackPosition (i);
        const int   layers = looper.getTrackLayers (i);
        const bool  muted  = looper.isMuted (i);

        auto ringRect = t.ringArea;   // local copy: we carve the label off it

        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Semibold")));
        g.drawText ("TRACK " + juce::String (i + 1),
                    ringRect.removeFromTop (16), juce::Justification::centred);

        const auto  ra    = ringRect.toFloat();
        const float ringR = juce::jmin (ra.getWidth(), ra.getHeight()) * 0.5f - 10.0f;
        const juce::Point<float> centre (ra.getCentreX(), ra.getCentreY());

        if (ringR < 12.0f)
            continue;

        g.setColour (theme.controlTrack);
        g.drawEllipse (centre.x - ringR, centre.y - ringR, ringR * 2.0f, ringR * 2.0f, 4.0f);

        const juce::Colour stateColour =
            st == LoopStation::Recording ? juce::Colour (0xffff453a)
          : st == LoopStation::Overdub   ? theme.waveform
          : st == LoopStation::Playing   ? theme.accent
          : theme.textSecondary;

        if (st != LoopStation::Empty)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f,
                               0.0f, juce::MathConstants<float>::twoPi
                                         * juce::jmax (0.02f, posN),
                               true);
            const float alpha = muted ? 0.35f : 1.0f;
            if (theme.glow >= 0.9f)
            {
                g.setColour (stateColour.withAlpha (0.25f * alpha));
                g.strokePath (arc, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
            }
            g.setColour (stateColour.withAlpha (alpha));
            g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        g.setColour (muted ? theme.textSecondary : theme.text);
        g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Semibold")));
        const juce::String stateText =
            st == LoopStation::Empty     ? "-"
          : st == LoopStation::Recording ? "REC"
          : st == LoopStation::Overdub   ? "DUB"
          : st == LoopStation::Playing   ? (muted ? "MUTE" : "PLAY")
          : "STOP";
        g.drawText (stateText,
                    juce::Rectangle<float> (ringR * 2.0f, 20.0f).withCentre (centre),
                    juce::Justification::centred);

        if (layers > 0)
        {
            g.setColour (theme.textSecondary);
            g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Medium")));
            g.drawText ("x" + juce::String (layers),
                        juce::Rectangle<float> (ringR * 2.0f, 14.0f)
                            .withCentre ({ centre.x, centre.y + 18.0f }),
                        juce::Justification::centred);
        }
    }

    // --- How-to line ---------------------------------------------------------
    g.setColour (theme.textSecondary);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("T1 sets the loop length - other tracks lock to it.   REC > SET > OVERDUB.   UNDO kills the last dub.   Keys below stay live.",
                card.reduced (14.0f).removeFromBottom (66.0f).removeFromTop (16.0f),
                juce::Justification::centred);
}
