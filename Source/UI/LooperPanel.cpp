#include "LooperPanel.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"
#include "../AudioEngine/SampleLoader.h"

namespace
{
    // Every word the record pad can ever show. resized() measures the widest
    // of them, so the pad is sized from its own vocabulary rather than from a
    // guess - which is how "OVERDUB" ended up cut in half on a 42px pad.
    const char* const kPadLabels[] = { "Rec", "Set", "Dub", "Play", "Go", "Arm" };

    // The five per-track buttons, in row order. Undo also shows "Redo", which
    // is narrower, so measuring "Undo" covers both states.
    const char* const kTrackLabels[] = { "Re-rec", "Undo", "Rev", "Mute", "Clr" };

    /** MUST match AppleLookAndFeel::getTextButtonFont: the width arithmetic
        below is only honest if it measures the font that actually draws. That
        look-and-feel paints button text with drawText(..., useEllipses=true)
        across the button's whole width, so "fits" means text width <= width. */
    juce::Font buttonFontFor (int buttonHeight)
    {
        return juce::Font (juce::FontOptions ((float) juce::jmin (15, buttonHeight - 8))
                               .withStyle ("Semibold"));
    }

    int textWidthFor (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidthInt (f, s);
    }
}

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
            // pass — starts in the track's own pre-picked sound, so six
            // tracks really are six independently-set instruments.
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

        // Rotary, not a bar. In a row layout a horizontal slider needs width
        // this lane does not have - squeezed into its slot it rendered as a
        // coloured dot with no readable position. The spec asks for knobs here
        // and a knob is what fits.
        t.volSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
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

        t.panSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
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
                              "overdub here.  Top entry loads an audio file straight into the track");
        t.mainButton.setTooltip ("1st tap: record.  2nd tap: lock the loop.  Then tap to stack overdubs / play");
        t.rerecButton.setTooltip ("Wipe this track and record it again in one tap");
        t.undoButton.setTooltip ("Remove the last overdub - press again to bring it back (Redo)");
        t.muteButton.setTooltip ("Mute this track");
        t.clearButton.setTooltip ("Clear - delete this track's loop");
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
    clearAllButton.setTooltip ("Delete every loop on every track");
    exportButton.setTooltip ("Save everything you looped as a WAV file (one full cycle)");
    addTrackButton.setTooltip ("Show another loop track - all "
                               + juce::String (LoopStation::kNumTracks)
                               + " are already on screen");
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
                "Record or load a loop first, then drag it into your DAW.");
        else
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon, "Slyce",
                "Hold this button and drag into your DAW's arrangement to drop "
                "the loop as a WAV. (Export WAV saves it to a folder instead.)");
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

    // The slider is only the grip. The VALUE is painted beside it as a large
    // mono numeral (spec 4.9 / 1: mono is reserved for numbers), so the stock
    // text box would just be a second, smaller copy of the same reading.
    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setValue (proc.getLooper().getMetroBpm(), juce::dontSendNotification);
    bpmSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    bpmSlider.setTooltip ("Drag to set the loop tempo");
    bpmSlider.onValueChange = [this]
    {
        proc.getLooper().setMetroBpm ((float) bpmSlider.getValue());
        repaint (bpmValueArea);
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

    // Repaint the mono numeral only when the reading actually moves.
    {
        const float bpmNow = proc.getLooper().getMetroBpm();
        if (std::abs (bpmNow - shownBpm) > 0.005f)
        {
            shownBpm = bpmNow;
            if (! bpmValueArea.isEmpty())
                repaint (bpmValueArea);
        }
    }

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
        // Sentence case, and never longer than the pad is wide - resized()
        // sizes the pad from exactly this set of words, so "Overdub" (which
        // used to be cut in half on the pad) is now the three-letter "Dub"
        // the ring itself already used.
        switch (st)
        {
            case LoopStation::Empty:     t.mainButton.setButtonText ("Rec");  break;
            case LoopStation::Recording: t.mainButton.setButtonText ("Set");  break;
            case LoopStation::Playing:   t.mainButton.setButtonText ("Dub");  break;
            case LoopStation::Overdub:   t.mainButton.setButtonText ("Play"); break;
            case LoopStation::Stopped:   t.mainButton.setButtonText ("Go");   break;
            case LoopStation::Armed:     t.mainButton.setButtonText ("Arm");  break;
            default: break;
        }
        t.undoButton.setButtonText (looper.isRedo (i) ? "Redo" : "Undo");
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
    auto area = getLocalBounds().reduced (20, 12);

    // The looper covers the editor's mid-section, and on the artwork skin that
    // section is only ~400px tall against ~695px on every other theme. Six
    // lanes plus a header, the sound pickers and a footer do not fit there at
    // full size, so the chrome steps down first and the lanes take the rest.
    const bool tight = area.getHeight() < 460;

    // --- Header line (spec 4.9) -------------------------------------------
    // "Loop station" on the left, tempo + transport on the right. Every
    // button width here is MEASURED against the font that draws it, so
    // "Clear all" can never come back as "Cl...".
    {
        auto head = area.removeFromTop (tight ? 40 : 48);
        const int headBtnH = tight ? 28 : 32;
        const juce::Font headFont = buttonFontFor (headBtnH);

        auto place = [&head, headBtnH] (juce::Component& c, int w)
        {
            c.setBounds (head.removeFromRight (w).withSizeKeepingCentre (w, headBtnH));
        };
        auto wideEnough = [&headFont] (const char* s, int minW)
        {
            return juce::jmax (minW, textWidthFor (headFont, s) + 20);
        };

        place (clearAllButton, wideEnough ("Clear all", 74));
        head.removeFromRight (6);
        place (stopAllButton,  wideEnough ("Stop all", 70));
        head.removeFromRight (6);
        place (playAllButton,  wideEnough ("Play all", 70));
        head.removeFromRight (12);
        place (metroButton,    wideEnough ("Click", 56));
        head.removeFromRight (6);
        place (tapButton,      wideEnough ("Tap", 46));
        head.removeFromRight (6);
        place (syncButton,     wideEnough ("Sync", 56));
        head.removeFromRight (8);

        const int sliderW = juce::jlimit (70, 110, head.getWidth() / 5);
        bpmSlider.setBounds (head.removeFromRight (sliderW)
                                 .withSizeKeepingCentre (sliderW, headBtnH));
        head.removeFromRight (6);

        // "BPM" is a label, so it stays in the UI face; the reading itself is
        // mono (spec 1 reserves mono for numbers).
        const juce::Font unitFont (juce::FontOptions (9.5f).withStyle ("Semibold"));
        bpmUnitArea = head.removeFromRight (textWidthFor (unitFont, "BPM") + 6);
        head.removeFromRight (3);

        const juce::Font numFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                     tight ? 17.0f : 20.0f,
                                                     juce::Font::plain));
        bpmValueArea = head.removeFromRight (textWidthFor (numFont, "240.0") + 8);
        head.removeFromRight (14);

        if (head.getWidth() > 70)
        {
            headerTickArea  = head.removeFromLeft (3).withSizeKeepingCentre (3, tight ? 12 : 14);
            head.removeFromLeft (8);
            headerTitleArea = head.removeFromTop (head.getHeight() * 55 / 100);
            headerSubArea   = head;
        }
        else
        {
            headerTickArea = headerTitleArea = headerSubArea = {};
        }
    }

    area.removeFromTop (tight ? 4 : 8);

    // --- Current-sound pickers --------------------------------------------
    // Captioned: two bare combos side by side told nobody which one picked
    // the sound.
    {
        auto pickers   = area.removeFromTop (tight ? 40 : 46);
        auto pickStrip = pickers.withSizeKeepingCentre (juce::jmin (460, pickers.getWidth()),
                                                        pickers.getHeight());
        pickCaptions.clear();
        auto capRow = pickStrip.removeFromTop (13);
        pickCaptions.push_back ({ "Engine",     capRow.removeFromLeft (110) });
        capRow.removeFromLeft (12);
        pickCaptions.push_back ({ "Instrument", capRow });
        pickStrip.removeFromTop (2);
        engineBox.setBounds (pickStrip.removeFromLeft (110));
        pickStrip.removeFromLeft (12);
        instrumentBox.setBounds (pickStrip);
    }

    // --- Footer (spec 4.9): the drag slab, with Export WAV / + Add track
    //     stacked in a narrow column beside it. Stacking them is what buys
    //     the slab its width back - side by side, the three of them plus the
    //     transport could not share one row without the slab's sub-line
    //     being cut off mid-sentence.
    {
        auto footer = area.removeFromBottom (tight ? 44 : 52);
        const int stackH = juce::jmax (24, footer.getHeight() - 4);
        const int stackBtnH = juce::jlimit (15, 24, (stackH - 6) / 2);
        const juce::Font stackFont = buttonFontFor (stackBtnH);

        int colW = 0;
        for (auto* s : { "Export WAV", "+ Add track" })
            colW = juce::jmax (colW, textWidthFor (stackFont, s));
        colW = juce::jlimit (100, 200, colW + 22);
        colW = juce::jmin (colW, juce::jmax (60, footer.getWidth() - 180));

        auto col = footer.removeFromRight (colW)
                         .withSizeKeepingCentre (colW, stackBtnH * 2 + 6);
        footer.removeFromRight (10);
        exportButton.setBounds (col.removeFromTop (stackBtnH));
        col.removeFromTop (6);
        addTrackButton.setBounds (col.removeFromTop (stackBtnH));
        dragButton.setBounds (footer.reduced (0, 1));
    }

    // --- How-to line (painted). First thing to go when space is short.
    if (tight)
    {
        howToArea = {};
        area.removeFromBottom (4);
    }
    else
    {
        howToArea = area.removeFromBottom (22);
        area.removeFromBottom (6);
    }

    // ONE ROW PER TRACK, stacked - which is what a loop station is, and what
    // the spec asks for. These used to be vertical columns side by side, and
    // that shape fought the content the whole way: six columns across a 1000px
    // panel gave each track about 160px, so the five per-track buttons were
    // squeezed to 30px each and their labels clipped, and a track's recorded
    // audio had nowhere to be shown at all. A row has the width for all of it
    // and lines the tracks up against each other, which is the comparison
    // anyone stacking loops is actually making.
    //
    // The floor came down from 44 to 30: six lanes at 44 plus the chrome
    // overflowed the short (artwork-skin) panel by ~70px, and a lane that
    // runs off the bottom of the card is worse than a short one. With gap 5
    // the clamp only bites below 6*30 + 5*5 = 205px of lane space, and the
    // tightest panel this thing gets is ~250.
    const int gap  = tight ? 5 : 8;
    const int rowH = juce::jlimit (30, 78,
                                   (area.getHeight() - gap * (visibleTracks - 1))
                                       / juce::jmax (1, visibleTracks));

    // Centre the block of lanes rather than letting them hug the top: rows are
    // capped at 78px, so any leftover space would otherwise all be dumped
    // underneath and the panel would read as half-empty.
    {
        const int used = rowH * visibleTracks + gap * (visibleTracks - 1);
        if (area.getHeight() > used)
            area = area.withSizeKeepingCentre (area.getWidth(), used);
    }

    // The record pad is sized from the longest word it can ever show, not
    // from the row height alone - at six lanes the row is shorter and a
    // square pad stopped being wide enough for "Play".
    const int padInset = juce::jmax (5, rowH / 5);
    const int padBtnH  = juce::jmax (12, rowH - padInset * 2);
    const juce::Font padFont = buttonFontFor (padBtnH);
    int padTextW = 0;
    for (auto* s : kPadLabels)
        padTextW = juce::jmax (padTextW, textWidthFor (padFont, s));
    const int padW = juce::jmax (rowH - 6, padTextW + padInset * 2 + 8);

    // Same treatment for the five per-track buttons: "Re-rec" is the widest
    // and it sets the column for all of them.
    const int ctlH = juce::jmax (14, juce::jmin (26, rowH - 10));
    const juce::Font ctlFont = buttonFontFor (ctlH);
    int trackBtnW = 34;
    for (auto* s : kTrackLabels)
        trackBtnW = juce::jmax (trackBtnW, textWidthFor (ctlFont, s) + 12);

    for (int i = 0; i < LoopStation::kNumTracks; ++i)
    {
        auto& t = trackUI[i];
        if (i >= visibleTracks)
        {
            t.laneArea = t.indexArea = t.ringArea = t.waveArea = {};
            continue;
        }

        auto row = area.removeFromTop (rowH);
        if (i < visibleTracks - 1)
            area.removeFromTop (gap);

        // The lane is the WHOLE row. Washing only part of it left the five
        // buttons and the two knobs sitting outside the thing they belong to,
        // so a track read as a strip plus some loose controls beside it.
        t.laneArea  = row;
        row         = row.reduced (10, 0);
        t.indexArea = row.removeFromLeft (24);
        t.ringArea  = row.removeFromLeft (juce::jmin (padW, juce::jmax (24, row.getWidth() / 3)));
        // The pad IS the ring: the button sits inside it so the progress
        // sweep reads as this control's own state, not as decoration near it.
        t.mainButton.setBounds (t.ringArea.reduced (padInset));
        row.removeFromLeft (10);

        const int comboW = juce::jlimit (96, 168, row.getWidth() / 5);
        t.instBox.setBounds (row.removeFromLeft (comboW)
                                .withSizeKeepingCentre (comboW, juce::jmin (30, rowH - 8)));
        row.removeFromLeft (10);

        // Right-hand controls first, so the waveform takes whatever is left
        // rather than pushing them off the end on a narrow panel.
        //
        // SQUARE knobs. These are rotary sliders, and a rotary in a 40x26 box
        // draws a 26px dial with 14px of dead space either side - which is why
        // they came out as unreadable smudges rather than as knobs.
        const int kn = juce::jlimit (32, 42, rowH - 8);
        t.volSlider.setBounds (row.removeFromRight (kn + 4)
                                  .withSizeKeepingCentre (kn, kn));
        t.panSlider.setBounds (row.removeFromRight (kn + 4)
                                  .withSizeKeepingCentre (kn, kn));
        row.removeFromRight (8);

        juce::TextButton* small[5] = { &t.rerecButton, &t.undoButton, &t.revButton,
                                       &t.muteButton,  &t.clearButton };
        const int bw = juce::jmax (34, juce::jmin (trackBtnW, (row.getWidth() - 40) / 5 - 4));
        auto btns = row.removeFromRight (bw * 5 + 4 * 4);
        for (int b = 0; b < 5; ++b)
        {
            small[b]->setBounds (btns.removeFromLeft (bw)
                                     .withSizeKeepingCentre (bw, ctlH));
            if (b < 4) btns.removeFromLeft (4);
        }
        row.removeFromRight (10);

        t.waveArea = row;   // whatever is left shows what this track holds
    }
}

void LooperPanel::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();

    // ComboBox draws its "nothing selected" placeholder itself, straight from
    // ComboBox::textColourId, which defaults to white and knows nothing about
    // the theme. Every unset track combo in here therefore read as white text
    // on a white field in the light themes - the label was there, it was just
    // the same colour as the panel. Refreshed on paint so it follows a theme
    // change rather than being frozen at construction.
    for (auto& t : trackUI)
    {
        t.instBox.setColour (juce::ComboBox::textColourId, theme.text);
        t.instBox.setColour (juce::ComboBox::arrowColourId, theme.textSecondary);

        // Pan and Vol are plain JUCE rotaries rather than our KnobComponent,
        // so nothing had ever given them theme colours. The stock defaults
        // draw the track in a colour that vanishes against this panel, which
        // left only the thumb visible - two purple dots where two knobs should
        // be. The dial was the right size the whole time; it was invisible.
        for (auto* s : { &t.panSlider, &t.volSlider })
        {
            s->setColour (juce::Slider::rotarySliderFillColourId,    theme.accent);
            s->setColour (juce::Slider::rotarySliderOutlineColourId, theme.controlTrack);
            s->setColour (juce::Slider::thumbColourId,               theme.text);
        }
    }
    for (auto* cb : { &engineBox, &instrumentBox })
    {
        cb->setColour (juce::ComboBox::textColourId, theme.text);
        cb->setColour (juce::ComboBox::arrowColourId, theme.textSecondary);
    }

    auto card = getLocalBounds().toFloat().reduced (2.0f);
    const float radius = theme.cornerRadius;

    // Opaque glass card — this tab covers the studio controls beneath it.
    g.setColour (theme.glow >= 0.9f ? juce::Colour (0xf0070b1e)
                                    : theme.bgTop);
    g.fillRoundedRectangle (card, radius);
    g.setColour (theme.accent.withAlpha (theme.glow >= 0.9f ? 0.35f : 0.15f));
    g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.2f);

    // --- Panel header (spec 4.9) ---------------------------------------------
    // The loop station is one of the three accent-forward surfaces, so unlike
    // every other module header its tick is the accent rather than a neutral.
    if (! headerTitleArea.isEmpty())
    {
        g.setColour (theme.accent);
        g.fillRoundedRectangle (headerTickArea.toFloat(), 1.5f);

        g.setColour (theme.text);
        g.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Semibold")));
        g.drawText ("Loop station", headerTitleArea,
                    juce::Justification::centredLeft, false);

        // ASCII separator on purpose: UTF-8 literals come out of this build as
        // Latin-1, so a middle dot would render as garbage.
        const juce::Font subFont (juce::FontOptions (11.0f));
        juce::String sub = juce::String (LoopStation::kNumTracks)
                             + " tracks - track 1 sets the length";
        if (textWidthFor (subFont, sub) > headerSubArea.getWidth())
            sub = "Track 1 sets the length";

        g.setColour (theme.textSecondary);
        g.setFont (subFont);
        g.drawText (sub, headerSubArea, juce::Justification::centredLeft, false);
    }

    // The tempo READS as a numeral, in mono, with the slider beside it as the
    // grip. A bare slider made the one number in this panel unreadable.
    if (! bpmValueArea.isEmpty())
    {
        g.setColour (theme.accent);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                  bpmValueArea.getHeight() >= 44 ? 20.0f : 17.0f,
                                                  juce::Font::plain)));
        g.drawText (juce::String (proc.getLooper().getMetroBpm(), 1), bpmValueArea,
                    juce::Justification::centredRight, false);

        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Semibold")));
        g.drawText ("BPM", bpmUnitArea, juce::Justification::centredLeft, false);
    }

    g.setColour (theme.textSecondary.withAlpha (0.75f));
    g.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Semibold")));
    for (const auto& c : pickCaptions)
        g.drawText (c.first, c.second, juce::Justification::centred, false);

    auto& looper = proc.getLooper();

    /** The strip showing what a track actually holds.

        A loop station where every track looks identical whether it is empty,
        armed or four layers deep is not showing you your arrangement, and that
        is what the column layout forced - there was no room for this at all.
        Empty tracks get a dashed outline that reads as a slot; recorded ones
        get a block with a playhead. */
    auto drawTrackWave = [&] (juce::Graphics& gg, juce::Rectangle<int> area, int idx,
                              int state, float posN, int layers, bool muted)
    {
        if (area.getWidth() < 40 || area.getHeight() < 14)
            return;

        auto r = area.toFloat().reduced (2.0f, 6.0f);

        if (state == LoopStation::Empty)
        {
            juce::Path dash;
            dash.addRoundedRectangle (r, 6.0f);
            const float pattern[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dash, dash, pattern, 2);
            gg.setColour (theme.separator);
            gg.fillPath (dash);

            gg.setColour (theme.textSecondary.withAlpha (0.75f));
            gg.setFont (juce::Font (juce::FontOptions (10.5f)));
            gg.drawText (idx == 0 ? "Empty - record here first, it sets the loop length"
                                  : "Empty",
                         area, juce::Justification::centred, false);
            return;
        }

        // Filled block. Height stands in for layer count, so an overdubbed
        // track is visibly denser than a single pass.
        const float fill = juce::jlimit (0.35f, 1.0f, 0.35f + 0.16f * (float) layers);
        auto body = r.withSizeKeepingCentre (r.getWidth(), r.getHeight() * fill);
        gg.setColour ((muted ? theme.textSecondary : theme.waveform)
                          .withAlpha (muted ? 0.28f : 0.55f));
        gg.fillRoundedRectangle (body, 4.0f);

        if (state != LoopStation::Stopped)
        {
            const float x = r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, posN);
            gg.setColour (theme.accent.withAlpha (muted ? 0.4f : 1.0f));
            gg.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
        }

        if (layers > 1)
        {
            gg.setColour (theme.text.withAlpha (0.8f));
            gg.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Bold")));
            gg.drawText ("x" + juce::String (layers), area.reduced (6, 0),
                         juce::Justification::centredRight, false);
        }

        // Track 1 defines how long every other track is - the one piece of
        // asymmetry in the six, and worth saying on the track itself.
        if (idx == 0)
        {
            gg.setColour (theme.accent.withAlpha (0.9f));
            gg.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Semibold")));
            gg.drawText ("Sets loop length", area.reduced (8, 0),
                         juce::Justification::centredLeft, false);
        }
    };

    for (int i = 0; i < visibleTracks; ++i)
    {
        const auto& t = trackUI[i];
        if (t.ringArea.isEmpty())
            continue;

        const int   st     = looper.getTrackState (i);
        const float posN   = looper.getTrackPosition (i);
        const int   layers = looper.getTrackLayers (i);
        const bool  muted  = looper.isMuted (i);

        // Lane wash: the row a track owns, so six of them read as six lanes
        // rather than as loose controls. The recording row lifts to accent.
        {
            auto lane = t.laneArea.toFloat();
            const bool rec = st == LoopStation::Recording || st == LoopStation::Overdub;
            g.setColour (rec ? theme.accent.withAlpha (0.09f)
                             : theme.material.withAlpha (theme.dark ? 0.45f : 0.55f));
            g.fillRoundedRectangle (lane, 10.0f);
            g.setColour (rec ? theme.accent.withAlpha (0.55f) : theme.separator);
            g.drawRoundedRectangle (lane.reduced (0.5f), 10.0f, 1.0f);
        }

        // Track number, at the head of its own lane. Mono, because spec 1
        // reserves the mono face for numerals.
        g.setColour (st == LoopStation::Empty ? theme.textSecondary : theme.text);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                  14.0f, juce::Font::bold)));
        g.drawText (juce::String (i + 1), t.indexArea, juce::Justification::centred);

        drawTrackWave (g, t.waveArea, i, st, posN, layers, muted);

        const auto  ra    = t.ringArea.toFloat();
        const float ringR = juce::jmin (ra.getWidth(), ra.getHeight()) * 0.5f - 6.0f;
        const juce::Point<float> centre (ra.getCentreX(), ra.getCentreY());

        // Below this the ring is narrower than the record pad sitting in it,
        // and a ring smaller than its own button reads as a mistake. Short
        // lanes (six tracks on the artwork skin) simply lose the ring and keep
        // the pad - the lane wash and the strip still show the state.
        if (ringR < 22.0f)
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
          : st == LoopStation::Recording ? "Rec"
          : st == LoopStation::Armed     ? "Arm"
          : st == LoopStation::Overdub   ? "Dub"
          : st == LoopStation::Playing   ? (muted ? "Mute" : "Play")
          : "Stop";
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
    // ASCII only, and the longest wording that fits: this build renders UTF-8
    // literals as Latin-1, and drawText would otherwise clip mid-word.
    if (! howToArea.isEmpty())
    {
        const juce::Font hintFont (juce::FontOptions (12.0f));
        juce::String hint = "Rec waits for the loop top (or a 1-bar count-in with Click on).   "
                            "Undo flips to Redo.   Rev plays a track backwards.   Tap sets the BPM.";
        if (textWidthFor (hintFont, hint) > howToArea.getWidth())
            hint = "Rec waits for the loop top.   Undo flips to Redo.   Tap sets the BPM.";
        if (textWidthFor (hintFont, hint) > howToArea.getWidth())
            hint = "Rec waits for the loop top.   Tap sets the BPM.";

        g.setColour (theme.textSecondary);
        g.setFont (hintFont);
        g.drawText (hint, howToArea, juce::Justification::centred, false);
    }
}
