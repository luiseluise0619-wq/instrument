#include "LooperPanel.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"
#include "../AudioEngine/SampleLoader.h"

LooperPanel::LooperPanel (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    // Looper controls must fire the instant the mouse goes down — timing IS
    // the feature.
    for (int i = 0; i < LoopStation::kNumTracks; ++i)
    {
        auto& t = trackUI[i];

        t.mainButton.setTriggeredOnMouseDown (true);
        t.rerecButton.setTriggeredOnMouseDown (true);
        t.undoButton.setTriggeredOnMouseDown (true);
        t.clearButton.setTriggeredOnMouseDown (true);

        t.mainButton.onClick = [this, i]
        {
            // Every take on this track — fresh recording OR a new overdub
            // pass — starts in the track's own pre-picked sound, so four
            // tracks really are four independently-set instruments.
            const int st = proc.getLooper().getTrackState (i);
            if (st == LoopStation::Empty || st == LoopStation::Playing)
                applyTrackInstrument (i);
            proc.getLooper().tapMain (i);
        };
        t.rerecButton.onClick = [this, i]
        {
            applyTrackInstrument (i);
            proc.getLooper().tapReRecord (i);
        };
        t.undoButton.onClick  = [this, i] { proc.getLooper().tapUndo (i);  };
        t.clearButton.onClick = [this, i] { proc.getLooper().tapClear (i); };

        t.muteButton.setClickingTogglesState (true);
        t.muteButton.setToggleState (proc.getLooper().isMuted (i),
                                     juce::dontSendNotification);
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

        t.revButton.setClickingTogglesState (true);
        t.revButton.setToggleState (proc.getLooper().isReversed (i),
                                    juce::dontSendNotification);
        t.revButton.onClick = [this, i]
        {
            proc.getLooper().setReversed (i, trackUI[i].revButton.getToggleState());
        };

        t.panSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        t.panSlider.setRange (-1.0, 1.0, 0.01);
        t.panSlider.setValue (proc.getLooper().getPan (i), juce::dontSendNotification);
        t.panSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        t.panSlider.setDoubleClickReturnValue (true, 0.0);   // centre on dbl-click
        t.panSlider.onValueChange = [this, i]
        {
            proc.getLooper().setPan (i, (float) trackUI[i].panSlider.getValue());
        };

        populateInstrumentBox (t.instBox, false);
        t.instBox.setTextWhenNothingSelected ("Sound " + juce::String (i + 1));
        t.instBox.onChange = [this, i]
        {
            const int id = trackUI[i].instBox.getSelectedId();
            if (id == kLoadAudioId)
            {
                // Deselect first so the combo doesn't sit on "Load Audio...".
                trackUI[i].instBox.setSelectedId (0, juce::dontSendNotification);
                importAudioToTrack (i);
                return;
            }
            trackUI[i].chosenInstrument = id > 0 ? id - 1 : -1;

            // Audition right away — but never while a take is rolling:
            // switching the global sound mid-Recording/Overdub would bake
            // the wrong instrument into another track's loop.
            auto& looper = proc.getLooper();
            for (int tr = 0; tr < LoopStation::kNumTracks; ++tr)
            {
                const int st = looper.getTrackState (tr);
                if (st == LoopStation::Recording || st == LoopStation::Overdub)
                    return;
            }
            applyTrackInstrument (i);
        };

        t.instBox.setTooltip ("This track's own sound - applied automatically when you record or "
                              "overdub here.  Top entry loads an audio FILE straight into the track");
        t.mainButton.setTooltip ("1st tap: record.  2nd tap: lock the loop.  Then tap to stack overdubs / play");
        t.rerecButton.setTooltip ("Wipe this track and record it again in one tap");
        t.undoButton.setTooltip ("Remove the last overdub - press again to bring it back (REDO)");
        t.muteButton.setTooltip ("Mute this track");
        t.clearButton.setTooltip ("Delete this track's loop");
        t.revButton.setTooltip ("Play this track backwards");
        t.panSlider.setTooltip ("Pan left/right (double-click = centre)");
        t.volSlider.setTooltip ("Track volume");

        addAndMakeVisible (t.instBox);
        addAndMakeVisible (t.mainButton);
        addAndMakeVisible (t.rerecButton);
        addAndMakeVisible (t.undoButton);
        addAndMakeVisible (t.clearButton);
        addAndMakeVisible (t.muteButton);
        addAndMakeVisible (t.revButton);
        addAndMakeVisible (t.panSlider);
        addAndMakeVisible (t.volSlider);
    }

    playAllButton.setTriggeredOnMouseDown (true);
    stopAllButton.setTriggeredOnMouseDown (true);
    playAllButton.onClick  = [this] { proc.getLooper().tapPlayAll();  };
    stopAllButton.onClick  = [this] { proc.getLooper().tapStopAll();  };
    clearAllButton.onClick = [this] { proc.getLooper().tapClearAll(); };
    playAllButton.setTooltip ("Restart every track together from the top");
    stopAllButton.setTooltip ("Stop all tracks (loops are kept)");
    clearAllButton.setTooltip ("Delete ALL loops");
    exportButton.setTooltip ("Save everything you looped as a WAV file (one full cycle)");
    addTrackButton.setTooltip ("Show another loop track (up to 6)");
    metroButton.setTooltip ("Metronome click - heard, never recorded. First take gets a 1-bar count-in");
    tapButton.setTooltip ("Tap in time to set the tempo");

    syncButton.setClickingTogglesState (true);
    syncButton.setToggleState (true, juce::dontSendNotification);   // on by default
    syncButton.setTooltip ("Follow the host tempo. Off = set the BPM by hand");

    dragButton.setTooltip ("Press and drag this into your DAW to drop the loop as a WAV");
    dragButton.makeFile = [this] { return writeMixToTempFile(); };
    dragButton.onClick  = [this]
    {
        // A plain click can't drag, so say what the button wants.
        if (! proc.getLooper().anyContent())
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon, "Slyce",
                "Record or load a loop first, then DRAG it into your DAW.");
        else
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon, "Slyce",
                "Hold this button and drag into your DAW's arrangement to drop "
                "the loop as a WAV. (EXPORT saves it to a folder instead.)");
    };

    exportButton.onClick = [this]
    {
        auto mix = std::make_shared<juce::AudioBuffer<float>>();
        if (! proc.getLooper().renderMixdown (*mix))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon, "Slyce",
                "Nothing to export yet - record or load a loop first.");
            return;
        }

        exportChooser = std::make_unique<juce::FileChooser> (
            "Export loops as WAV",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile ("slyce-loop.wav"),
            "*.wav");

        exportChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, mix] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File{})
                return;
            f = f.withFileExtension ("wav");

            auto writeWav = [this] (const juce::File& dest,
                                    const juce::AudioBuffer<float>& buf) -> bool
            {
                dest.deleteFile();
                juce::WavAudioFormat wav;
                auto stream = dest.createOutputStream();
                if (stream == nullptr)
                    return false;
                if (auto* writer = wav.createWriterFor (stream.get(),
                                                        proc.getLooper().getSampleRate(),
                                                        2, 24, {}, 0))
                {
                    std::unique_ptr<juce::AudioFormatWriter> w (writer);
                    stream.release();   // the writer owns the stream now
                    return w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
                }
                return false;
            };

            // Mix first, then one aligned stem per recorded track (same
            // length as the mix, so they drop into a DAW in sync). Report
            // exactly what landed on disk - a partial export must not be
            // presented as either total success or total failure.
            int written = 0, failed = 0;
            if (writeWav (f, *mix)) ++written; else ++failed;

            for (int ti = 0; ti < LoopStation::kNumTracks; ++ti)
            {
                juce::AudioBuffer<float> stem;
                if (proc.getLooper().renderMixdown (stem, ti))
                {
                    if (writeWav (f.getSiblingFile (
                            f.getFileNameWithoutExtension()
                            + "-track" + juce::String (ti + 1) + ".wav"), stem))
                        ++written;
                    else
                        ++failed;
                }
            }

            if (failed > 0)
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Slyce",
                    juce::String (written) + " file(s) were exported but "
                    + juce::String (failed) + " could not be written - "
                    "check free disk space or try another folder.");
            else
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon, "Slyce",
                    "Exported " + f.getFileName() + " plus "
                    + juce::String (written - 1) + " track stem(s) next to it.");
        });
    };
    addAndMakeVisible (exportButton);
    addAndMakeVisible (syncButton);
    addAndMakeVisible (dragButton);

    addAndMakeVisible (playAllButton);
    addAndMakeVisible (stopAllButton);
    addAndMakeVisible (clearAllButton);

    addTrackButton.onClick = [this]
    {
        visibleTracks = juce::jmin (LoopStation::kNumTracks, visibleTracks + 1);
        updateTrackVisibility();
        resized();
        repaint();
    };
    addAndMakeVisible (addTrackButton);

    metroButton.setClickingTogglesState (true);
    metroButton.setToggleState (proc.getLooper().isMetronomeOn(),
                                juce::dontSendNotification);
    metroButton.onClick = [this]
    {
        proc.getLooper().setMetronomeOn (metroButton.getToggleState());
    };
    addAndMakeVisible (metroButton);

    tapButton.setTriggeredOnMouseDown (true);   // tap timing must be exact
    tapButton.onClick = [this]
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double gap = now - lastTapMs;
        lastTapMs = now;
        if (gap > 60.0 && gap < 2000.0)   // 30..1000 BPM taps count
        {
            // Smooth over the last few taps so one sloppy hit doesn't yank
            // the tempo around.
            tapIntervalMs = tapCount == 0 ? gap : (tapIntervalMs * 0.6 + gap * 0.4);
            ++tapCount;
            const double bpm = juce::jlimit (40.0, 240.0, 60000.0 / tapIntervalMs);
            bpmSlider.setValue (bpm, juce::sendNotificationSync);
        }
        else
            tapCount = 0;   // too long a pause: start a fresh measurement
    };
    addAndMakeVisible (tapButton);

    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setValue (proc.getLooper().getMetroBpm(), juce::dontSendNotification);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 20);
    bpmSlider.setTextValueSuffix ("");
    bpmSlider.onValueChange = [this]
    {
        proc.getLooper().setMetroBpm ((float) bpmSlider.getValue());
    };
    addAndMakeVisible (bpmSlider);

    // --- Current-sound pickers (mirror the studio's engine + instrument) ---
    engineBox.addItem ("Chop", 1);
    engineBox.addItem ("Synth", 2);
    engineBox.addItem ("Sampled", 3);
    engineBox.addItem ("Melody", 4);
    engineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.getAPVTS(), "engine", engineBox);
    addAndMakeVisible (engineBox);

    populateInstrumentBox (instrumentBox, true);
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

    // Loops live in the processor and survive the editor: if tracks 5/6 are
    // still playing, reopening the window must show their strips again.
    for (int i = 0; i < LoopStation::kNumTracks; ++i)
        if (proc.getLooper().getTrackState (i) != LoopStation::Empty)
            visibleTracks = juce::jmax (visibleTracks, i + 1);

    updateTrackVisibility();
    startTimerHz (30);
}

LooperPanel::~LooperPanel()
{
    stopTimer();
}

void LooperPanel::populateInstrumentBox (juce::ComboBox& box, bool withQuickShelf)
{
    const auto names = VocalChopAudioProcessor::getInstrumentNames();
    const auto cats  = VocalChopAudioProcessor::getInstrumentCategories();
    auto* root = box.getRootMenu();

    if (! withQuickShelf)
    {
        // Per-track boxes only: drop a sample/loop file straight onto this
        // track instead of recording one.
        box.addItem ("Load Audio File...", kLoadAudioId);
        root->addSeparator();
    }

    if (withQuickShelf)
    {
        static const char* featured[] = { "Drum Kit", "Vox Choir", "Vox Pluck",
                                          "Supersaw Lead", "Rage Bell", "Memphis 808",
                                          "Soul Keys", "Kick 808", "Snare 808",
                                          "Clap", "Bass Pad", "Royal Grand" };
        root->addSectionHeader ("QUICK");
        for (auto* f : featured)
        {
            const int idx = names.indexOf (f);
            if (idx >= 0)
                box.addItem (f, 1000 + idx);
        }
        root->addSeparator();
    }

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

void LooperPanel::applyTrackInstrument (int track)
{
    const int instr = trackUI[track].chosenInstrument;
    if (instr >= 0 && instr != proc.getCurrentInstrument())
        proc.applyInstrument (instr);
}

void LooperPanel::updateTrackVisibility()
{
    for (int i = 0; i < LoopStation::kNumTracks; ++i)
    {
        const bool on = i < visibleTracks;
        auto& t = trackUI[i];
        t.instBox.setVisible (on);
        t.mainButton.setVisible (on);
        t.rerecButton.setVisible (on);
        t.undoButton.setVisible (on);
        t.clearButton.setVisible (on);
        t.muteButton.setVisible (on);
        t.revButton.setVisible (on);
        t.panSlider.setVisible (on);
        t.volSlider.setVisible (on);
    }
    addTrackButton.setEnabled (visibleTracks < LoopStation::kNumTracks);
}

void LooperPanel::timerCallback()
{
    // Follow the host tempo (VST3/AU in a DAW). Standalone reports none, so
    // the slider stays under the user's control there.
    if (syncButton.getToggleState())
    {
        const double bpm = proc.getHostBpm();
        if (bpm > 0.0 && std::abs (bpm - proc.getLooper().getMetroBpm()) > 0.01)
        {
            proc.getLooper().setMetroBpm ((float) bpm);
            bpmSlider.setValue (bpm, juce::dontSendNotification);
        }
    }
    bpmSlider.setEnabled (! syncButton.getToggleState() || proc.getHostBpm() <= 0.0);

    // Mirror an instrument change made anywhere else. Compare INDICES, not
    // raw ids — QUICK-shelf picks use ids 1000+idx, and comparing ids would
    // freeze the mirror forever after one QUICK selection.
    {
        const int sel    = instrumentBox.getSelectedId();
        const int selIdx = sel >= 1000 ? sel - 1000 : sel - 1;
        if (selIdx != proc.getCurrentInstrument())
            instrumentBox.setSelectedId (proc.getCurrentInstrument() + 1,
                                         juce::dontSendNotification);
    }

    // Keep the linear sliders on the theme accent (the stock JUCE blue thumb
    // clashes with every one of our palettes).
    {
        const auto& th = ThemeManager::active();
        auto styleSlider = [&th] (juce::Slider& s)
        {
            s.setColour (juce::Slider::thumbColourId, th.accent);
            s.setColour (juce::Slider::trackColourId, th.accent.withAlpha (0.45f));
            s.setColour (juce::Slider::backgroundColourId, th.controlTrack);
        };
        styleSlider (bpmSlider);
        for (auto& t : trackUI)
        {
            styleSlider (t.volSlider);
            styleSlider (t.panSlider);
        }
    }

    auto& looper = proc.getLooper();
    for (int i = 0; i < visibleTracks; ++i)
    {
        auto& t = trackUI[i];
        const int st = looper.getTrackState (i);
        switch (st)
        {
            case LoopStation::Empty:     t.mainButton.setButtonText ("REC");     break;
            case LoopStation::Recording: t.mainButton.setButtonText ("SET");     break;
            case LoopStation::Playing:   t.mainButton.setButtonText ("OVERDUB"); break;
            case LoopStation::Overdub:   t.mainButton.setButtonText ("PLAY");    break;
            case LoopStation::Stopped:   t.mainButton.setButtonText ("GO");      break;
            case LoopStation::Armed:     t.mainButton.setButtonText ("WAIT..."); break;
            default: break;
        }
        t.undoButton.setButtonText (looper.isRedo (i) ? "REDO" : "UNDO");
        t.undoButton.setEnabled (looper.canUndo (i));
        t.rerecButton.setEnabled (st != LoopStation::Empty && st != LoopStation::Armed);

        // Repaint ONLY the progress rings - a full-panel repaint at 30 Hz
        // (buttons, sliders, combos and all) was pure wasted CPU.
        if (! t.ringArea.isEmpty())
            repaint (t.ringArea);
    }
}

void LooperPanel::importAudioToTrack (int track)
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load audio into loop track " + juce::String (track + 1),
        juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
        [this, track] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        double srcRate = 44100.0;
        auto buf = SampleLoader::decode (file, srcRate);
        if (buf == nullptr
            || ! proc.getLooper().importAudio (track, *buf, srcRate))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon, "Slyce",
                "Could not load this audio file into the loop track.");
        }
    });
}

juce::File LooperPanel::writeMixToTempFile()
{
    juce::AudioBuffer<float> mix;
    if (! proc.getLooper().renderMixdown (mix))
        return {};

    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("Slyce-loop.wav");
    f.deleteFile();

    juce::WavAudioFormat wav;
    auto stream = f.createOutputStream();
    if (stream == nullptr)
        return {};

    if (auto* writer = wav.createWriterFor (stream.get(), proc.getLooper().getSampleRate(),
                                            2, 24, {}, 0))
    {
        std::unique_ptr<juce::AudioFormatWriter> w (writer);
        stream.release();
        if (w->writeFromAudioSampleBuffer (mix, 0, mix.getNumSamples()))
            return f;
    }
    return {};
}

void LooperPanel::resized()
{
    auto area = getLocalBounds().reduced (20);

    // Current-sound pickers along the top of the panel. Captioned like the
    // main strip: two bare combos side by side told nobody which one picked
    // the sound.
    auto pickers = area.removeFromTop (52);
    auto pickStrip = pickers.withSizeKeepingCentre (juce::jmin (460, pickers.getWidth()), 48);
    pickCaptions.clear();
    auto capRow = pickStrip.removeFromTop (13);
    pickCaptions.push_back ({ "ENGINE",     capRow.removeFromLeft (110) });
    capRow.removeFromLeft (12);
    pickCaptions.push_back ({ "INSTRUMENT", capRow });
    pickStrip.removeFromTop (1);
    engineBox.setBounds (pickStrip.removeFromLeft (110));
    pickStrip.removeFromLeft (12);
    instrumentBox.setBounds (pickStrip);

    // Master transport row at the bottom: MET + BPM, then the big three,
    // then + TRACK.
    auto master = area.removeFromBottom (50);
    auto mStrip = master.withSizeKeepingCentre (juce::jmin (880, master.getWidth()), 42);
    metroButton.setBounds (mStrip.removeFromLeft (58));
    mStrip.removeFromLeft (8);
    tapButton.setBounds (mStrip.removeFromLeft (52));
    mStrip.removeFromLeft (8);
    bpmSlider.setBounds (mStrip.removeFromLeft (juce::jmin (120, mStrip.getWidth() / 5)));
    mStrip.removeFromLeft (6);
    syncButton.setBounds (mStrip.removeFromLeft (58));
    mStrip.removeFromLeft (12);
    addTrackButton.setBounds (mStrip.removeFromRight (86));
    mStrip.removeFromRight (8);
    exportButton.setBounds (mStrip.removeFromRight (76));
    mStrip.removeFromRight (6);
    dragButton.setBounds (mStrip.removeFromRight (64));
    mStrip.removeFromRight (12);
    const int mw = juce::jmax (60, (mStrip.getWidth() - 24) / 3);
    playAllButton.setBounds  (mStrip.removeFromLeft (mw));
    mStrip.removeFromLeft (12);
    stopAllButton.setBounds  (mStrip.removeFromLeft (mw));
    mStrip.removeFromLeft (12);
    clearAllButton.setBounds (mStrip);

    area.removeFromBottom (24);   // how-to line (painted)
    area.removeFromTop (6);

    // Visible track strips side by side.
    const int gap = 12;
    const int stripW = (area.getWidth() - gap * (visibleTracks - 1)) / juce::jmax (1, visibleTracks);
    for (int i = 0; i < LoopStation::kNumTracks; ++i)
    {
        auto& t = trackUI[i];
        if (i >= visibleTracks)
        {
            t.ringArea = {};
            continue;
        }

        auto strip = area.removeFromLeft (stripW);
        if (i < visibleTracks - 1)
            area.removeFromLeft (gap);

        t.instBox.setBounds (strip.removeFromTop (30));
        strip.removeFromTop (4);

        auto vol = strip.removeFromBottom (24);
        t.volSlider.setBounds (vol.reduced (4, 0));

        auto revpan = strip.removeFromBottom (28);
        t.revButton.setBounds (revpan.removeFromLeft (48));
        revpan.removeFromLeft (4);
        t.panSlider.setBounds (revpan.reduced (2, 2));

        auto small = strip.removeFromBottom (34);
        const int sw = (small.getWidth() - 18) / 4;
        t.rerecButton.setBounds (small.removeFromLeft (sw));
        small.removeFromLeft (6);
        t.undoButton.setBounds  (small.removeFromLeft (sw));
        small.removeFromLeft (6);
        t.muteButton.setBounds  (small.removeFromLeft (sw));
        small.removeFromLeft (6);
        t.clearButton.setBounds (small);

        strip.removeFromBottom (6);
        t.mainButton.setBounds (strip.removeFromBottom (46));

        strip.removeFromBottom (4);
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

    g.setColour (theme.textSecondary.withAlpha (0.75f));
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Semibold"))
                   .withExtraKerningFactor (0.14f));
    for (const auto& c : pickCaptions)
        g.drawText (c.first, c.second, juce::Justification::centred, false);

    auto& looper = proc.getLooper();

    for (int i = 0; i < visibleTracks; ++i)
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
        // (the sound picker for this track sits directly above)

        const auto  ra    = ringRect.toFloat();
        const float ringR = juce::jmin (ra.getWidth(), ra.getHeight()) * 0.5f - 10.0f;
        const juce::Point<float> centre (ra.getCentreX(), ra.getCentreY());

        if (ringR < 12.0f)
            continue;

        // Recessed well inside the ring, so an empty track reads as a dial
        // waiting to be filled rather than an outline floating in space.
        {
            juce::ColourGradient well (theme.bgBottom.withAlpha (theme.dark ? 0.55f : 0.10f),
                                       centre.x, centre.y - ringR * 0.4f,
                                       theme.bgTop.withAlpha (0.0f),
                                       centre.x, centre.y + ringR, false);
            g.setGradientFill (well);
            g.fillEllipse (centre.x - ringR, centre.y - ringR, ringR * 2.0f, ringR * 2.0f);

            // Faint accent halo hugging the inside of the track ring.
            g.setColour (theme.accent.withAlpha (0.06f));
            g.drawEllipse (centre.x - ringR + 3.0f, centre.y - ringR + 3.0f,
                           (ringR - 3.0f) * 2.0f, (ringR - 3.0f) * 2.0f, 6.0f);
        }

        // Track ring, lit from the top like the rest of the panel.
        {
            juce::ColourGradient ring (theme.controlTrack.brighter (0.35f),
                                       centre.x, centre.y - ringR,
                                       theme.controlTrack.darker (0.25f),
                                       centre.x, centre.y + ringR, false);
            g.setGradientFill (ring);
            g.drawEllipse (centre.x - ringR, centre.y - ringR, ringR * 2.0f, ringR * 2.0f, 4.0f);
        }

        // Twelve o'clock index mark: the loop's top, so the sweep has a datum.
        g.setColour (theme.textSecondary.withAlpha (0.55f));
        g.fillRoundedRectangle (centre.x - 1.0f, centre.y - ringR - 5.0f, 2.0f, 7.0f, 1.0f);

        const juce::Colour stateColour =
            st == LoopStation::Recording ? juce::Colour (0xffff453a)
          : st == LoopStation::Armed     ? juce::Colour (0xffffd60a)
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
          : st == LoopStation::Armed     ? "ARM"
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
    g.drawText ("REC waits for the loop top (or a 1-bar count-in with MET on).   UNDO flips to REDO.   REV plays a track backwards.   TAP sets the BPM.",
                card.reduced (14.0f).removeFromBottom (64.0f).removeFromTop (16.0f),
                juce::Justification::centred);
}
