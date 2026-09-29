#include "LooperPanel.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"
#include "../AudioEngine/SampleLoader.h"

namespace
{
    // Every word the record pad can ever show. resized() measures the widest
    // of them, so the pad is sized from its own vocabulary rather than from a
    // guess - which is how "OVERDUB" ended up cut in half on a 42px pad.
    const char* const kPadLabels[] = { "Record", "Finish", "Dub", "Play", "Resume", "Arm" };

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
            selectedTrack = i;
            if (trackUI[i].usesCurrentChop)
            {
                if (auto* p = proc.getAPVTS().getParameter ("engine"))
                    p->setValueNotifyingHost (p->convertTo0to1 (0.0f)); // Chop
            }
            if (trackUI[i].chosenInstrument >= 0)
            {
                syncingInstrumentPickers = true;
                instrumentBox.setSelectedId (trackUI[i].chosenInstrument + 1,
                                             juce::dontSendNotification);
                syncingInstrumentPickers = false;
            }
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
            selectedTrack = i;
            if (trackUI[i].chosenInstrument >= 0)
            {
                syncingInstrumentPickers = true;
                instrumentBox.setSelectedId (trackUI[i].chosenInstrument + 1,
                                             juce::dontSendNotification);
                syncingInstrumentPickers = false;
            }
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
            selectedTrack = i;
            const int id = trackUI[i].instBox.getSelectedId();
            if (id == kCurrentChopId)
            {
                if (proc.getLoadedSample() == nullptr)
                {
                    trackUI[i].instBox.setSelectedId (0, juce::dontSendNotification);
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::InfoIcon, "Vocal Chop",
                        "Load a vocal sample in the main waveform first, then choose Current Vocal Chop.");
                    return;
                }
                trackUI[i].chosenInstrument = -1;
                trackUI[i].usesCurrentChop = true;
                if (auto* p = proc.getAPVTS().getParameter ("engine"))
                    p->setValueNotifyingHost (p->convertTo0to1 (0.0f)); // Chop
                trackUI[i].instBox.setTooltip (
                    "Uses the currently loaded vocal chop. Load a vocal sample above, then press Rec.");
                return;
            }
            trackUI[i].usesCurrentChop = false;
            if (id == kLoadAudioId)
            {
                // Deselect first so the combo doesn't sit on "Load Audio...".
                trackUI[i].instBox.setSelectedId (0, juce::dontSendNotification);
                importAudioToTrack (i);
                return;
            }
            trackUI[i].chosenInstrument = id > 0 ? id - 1 : -1;
            syncingInstrumentPickers = true;
            instrumentBox.setSelectedId (trackUI[i].chosenInstrument + 1,
                                         juce::dontSendNotification);
            syncingInstrumentPickers = false;

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
        t.mainButton.setTooltip ("Record: start a take. Finish: end and keep it stopped. Resume: play it. Dub: add a layer.");
        t.rerecButton.setTooltip ("Wipe this track and record it again in one tap");
        t.undoButton.setTooltip ("Remove the last overdub - press again to bring it back (Redo)");
        t.muteButton.setTooltip ("Mute this track");
        t.clearButton.setTooltip ("Clear - delete this track's loop");
        t.revButton.setTooltip ("Play this track backwards");
        t.moreButton.setTooltip ("Track actions: re-record, undo, reverse, mute, or clear");
        t.panSlider.setTooltip ("Pan left/right (double-click = centre)");
        t.volSlider.setTooltip ("Track volume");

        addAndMakeVisible (t.instBox);
        addAndMakeVisible (t.mainButton);
        addAndMakeVisible (t.moreButton);
        addChildComponent (t.rerecButton);
        addChildComponent (t.undoButton);
        addChildComponent (t.clearButton);
        addChildComponent (t.muteButton);
        addChildComponent (t.revButton);

        t.moreButton.onClick = [this, i]
        {
            auto& looper = proc.getLooper();
            juce::PopupMenu menu;
            menu.addItem (1, "Record this track again", looper.getTrackState (i) != LoopStation::Empty);
            menu.addItem (2, looper.isRedo (i) ? "Redo last layer" : "Undo last layer",
                          looper.canUndo (i));
            menu.addSeparator();
            menu.addItem (3, "Reverse playback", true, looper.isReversed (i));
            menu.addItem (4, "Mute track", true, looper.isMuted (i));
            menu.addItem (5, "Clear this track", looper.getTrackState (i) != LoopStation::Empty);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&trackUI[i].moreButton),
                [this, i] (int action)
                {
                    auto& loop = proc.getLooper();
                    switch (action)
                    {
                        case 1: applyTrackInstrument (i); loop.tapReRecord (i); break;
                        case 2: loop.tapUndo (i); break;
                        case 3: loop.setReversed (i, ! loop.isReversed (i)); break;
                        case 4: loop.setMuted (i, ! loop.isMuted (i)); break;
                        case 5: loop.tapClear (i); break;
                        default: break;
                    }
                    repaint();
                });
        };
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
    syncButton.setToggleState (proc.getLooper().isTempoSync(), juce::dontSendNotification);
    syncButton.setTooltip ("Sync click and NEW recording grid to host BPM. Existing audio is not time-stretched.");
    syncButton.onClick = [this] { proc.getLooper().setTempoSync (syncButton.getToggleState()); };
    midiButton.setTooltip ("Show the simple MIDI controller map");
    midiButton.onClick = [this]
    {
        auto* alert = new juce::AlertWindow ("Looper MIDI setup",
                                             "Choose a CC number for each action. Notes still play instruments.",
                                             juce::MessageBoxIconType::NoIcon);
        juce::StringArray ccItems;
        for (int cc = 0; cc < 128; ++cc) ccItems.add ("CC" + juce::String (cc));
        const juce::String names[] = {
            "Record track 1", "Record track 2", "Record track 3", "Record track 4",
            "Record track 5", "Record track 6", "Stop all", "Play all",
            "Previous track", "Next track", "Record selected", "Stop selected",
            "Re-record selected", "Undo / redo selected", "Clear selected" };
        for (int i = 0; i < 15; ++i)
        {
            alert->addComboBox ("midiCC" + juce::String (i), ccItems, names[i]);
            if (auto* box = alert->getComboBoxComponent ("midiCC" + juce::String (i)))
                box->setSelectedId (proc.getLooperMidiCC (i) + 1, juce::dontSendNotification);
        }
        alert->addButton ("Apply", 1);
        alert->addButton ("Cancel", 0);
        alert->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, alert] (int result)
            {
                if (result == 1)
                    for (int i = 0; i < 15; ++i)
                        if (auto* box = alert->getComboBoxComponent ("midiCC" + juce::String (i)))
                            proc.setLooperMidiCC (i, box->getSelectedId() - 1);
            }), true);
    };
    addAndMakeVisible (midiButton);

    dragButton.setTooltip ("Drag this striped area into the DAW timeline to drop a WAV. Maximum recording is 30 seconds per track.");
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
        // Capture once BEFORE the file dialog. All stems share this exact
        // recording, sample rate, phase, duration and gain even if the user
        // plays, imports, or changes devices while the dialog is open.
        struct ExportBundle
        {
            double rate = 44100.0;
            juce::AudioBuffer<float> mix;
            std::array<juce::AudioBuffer<float>, LoopStation::kNumTracks> stems;
            std::array<bool, LoopStation::kNumTracks> present {};
        };
        auto snapshot = proc.getLooper().captureSnapshot();
        auto bundle = std::make_shared<ExportBundle>();
        if (! snapshot || ! LoopStation::renderSnapshot (*snapshot, bundle->mix, -1, 0.0, false))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon, "Slyce",
                "No audible loop, or the common repeat cycle is longer than 120 seconds. "
                "Unmute a track or use loops with compatible lengths.");
            return;
        }
        bundle->rate = snapshot->getSampleRate();
        const double seconds = bundle->mix.getNumSamples() / bundle->rate;
        float peak = bundle->mix.getMagnitude (0, bundle->mix.getNumSamples());
        for (int ti = 0; ti < LoopStation::kNumTracks; ++ti)
        {
            bundle->present[ti] = LoopStation::renderSnapshot (*snapshot, bundle->stems[ti], ti, seconds, false);
            if (bundle->present[ti])
                peak = juce::jmax (peak, bundle->stems[ti].getMagnitude (0, bundle->stems[ti].getNumSamples()));
        }
        // Shared attenuation, including stems whose cancellation hid their
        // peak in the mix; muted stems are deliberately exported unmuted.
        if (peak > 0.891250938f)
        {
            const float gain = 0.891250938f / peak;
            bundle->mix.applyGain (gain);
            for (int ti = 0; ti < LoopStation::kNumTracks; ++ti)
                if (bundle->present[ti]) bundle->stems[ti].applyGain (gain);
        }
        exportChooser = std::make_unique<juce::FileChooser> (
            "Export loops as WAV",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("slyce-loop.wav"),
            "*.wav");
        juce::Component::SafePointer<LooperPanel> safe (this);
        exportChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, bundle] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult();
            if (f == juce::File{}) return;
            f = f.withFileExtension ("wav");
            const auto writeWav = [bundle] (const juce::File& dest,
                                            const juce::AudioBuffer<float>& buf) -> bool
            {
                juce::TemporaryFile temporary (dest);
                juce::WavAudioFormat wav;
                auto stream = temporary.getFile().createOutputStream();
                if (stream == nullptr) return false;
                auto* writer = wav.createWriterFor (stream.get(), bundle->rate, 2, 24, {}, 0);
                if (writer == nullptr) return false;
                stream.release();
                std::unique_ptr<juce::AudioFormatWriter> ownedWriter (writer);
                const bool ok = ownedWriter->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
                ownedWriter.reset(); // flush before replacing the destination
                return ok && temporary.overwriteTargetFileWithTemporary();
            };
            int written = 0, failed = 0;
            if (writeWav (f, bundle->mix)) ++written; else ++failed;
            for (int ti = 0; ti < LoopStation::kNumTracks; ++ti)
                if (bundle->present[ti])
                {
                    const auto dest = f.getSiblingFile (f.getFileNameWithoutExtension()
                                      + "-track" + juce::String (ti + 1) + ".wav");
                    if (writeWav (dest, bundle->stems[ti])) ++written; else ++failed;
                }
            juce::AlertWindow::showMessageBoxAsync (
                failed > 0 ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                "Slyce", juce::String (written) + " WAV file(s) exported; "
                           + juce::String (failed) + " failed. All files use the same snapshot and gain.");
        });
    };
    addChildComponent (exportButton); // available from the single Actions menu
    addAndMakeVisible (syncButton);
    addAndMakeVisible (dragButton);
    transportButton.onClick = [this]
    {
        if (proc.getLooper().anyRunning())
            proc.getLooper().tapStopAll();
        else
            proc.getLooper().tapPlayAll();
    };
    transportButton.setTooltip ("Play all loops; while playing, this stops every track");
    actionsButton.setTooltip ("More loop actions");
    actionsButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Clear all loops", proc.getLooper().anyContent());
        menu.addItem (2, "Export mix and tracks as WAV", proc.getLooper().anyContent());
        menu.addSeparator();
        menu.addItem (3, proc.getLooper().isMetronomeOn() ? "Turn metronome off"
                                                          : "Turn metronome on");
        menu.addItem (4, "MIDI controller map...");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&actionsButton),
            [this] (int item)
            {
                if (item == 1)
                    proc.getLooper().tapClearAll();
                else if (item == 2)
                    exportButton.triggerClick();
                else if (item == 3)
                {
                    const bool enabled = ! proc.getLooper().isMetronomeOn();
                    proc.getLooper().setMetronomeOn (enabled);
                    metroButton.setToggleState (enabled, juce::dontSendNotification);
                }
                else if (item == 4)
                    midiButton.triggerClick();
            });
    };
    addAndMakeVisible (transportButton);
    addAndMakeVisible (actionsButton);

    addTrackButton.onClick = [this]
    {
        visibleTracks = juce::jmin (LoopStation::kNumTracks, visibleTracks + 1);
        updateTrackVisibility();
        resized();
        repaint();
    };
    addChildComponent (addTrackButton); // all six tracks are already visible

    metroButton.setClickingTogglesState (true);
    metroButton.setToggleState (proc.getLooper().isMetronomeOn(),
                                juce::dontSendNotification);
    metroButton.onClick = [this]
    {
        proc.getLooper().setMetronomeOn (metroButton.getToggleState());
    };
    addAndMakeVisible (metroButton); // direct count-in toggle; also mirrored in Actions

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
    engineBox.addItem ("Mapped Sample", 4);
    engineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.getAPVTS(), "engine", engineBox);
    addAndMakeVisible (engineBox);

    populateInstrumentBox (instrumentBox, true);
    instrumentBox.setTextWhenNothingSelected ("Instrument");
    instrumentBox.onChange = [this]
    {
        if (syncingInstrumentPickers)
            return;

        const int id = instrumentBox.getSelectedId();
        const int instrument = id >= 1000 ? id - 1000 : id - 1;
        if (instrument < 0)
            return;

        // The compact picker above the tracks is an alternate way to choose
        // the selected track's sound, not a disconnected global patch menu.
        auto& track = trackUI[selectedTrack];
        track.chosenInstrument = instrument;
        syncingInstrumentPickers = true;
        track.instBox.setSelectedId (instrument + 1, juce::dontSendNotification);
        syncingInstrumentPickers = false;

        const auto& looper = proc.getLooper();
        bool takeInProgress = false;
        for (int tr = 0; tr < LoopStation::kNumTracks; ++tr)
        {
            const int state = looper.getTrackState (tr);
            if (state == LoopStation::Recording || state == LoopStation::Overdub)
                takeInProgress = true;
        }

        if (! takeInProgress)
            proc.applyInstrument (instrument);
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
            // Per-track boxes only: choose the currently loaded vocal chop, or
            // drop a rendered sample/loop file straight onto this track.
            box.addItem ("Current Vocal Chop", kCurrentChopId);
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
    if (trackUI[track].usesCurrentChop)
    {
        if (auto* p = proc.getAPVTS().getParameter ("engine"))
            p->setValueNotifyingHost (p->convertTo0to1 (0.0f)); // Chop
        return;
    }
    const int instr = trackUI[track].chosenInstrument;
    // The patch index alone is not enough. If Chop/Mapped Sample is active
    // and the track happens to choose the same numbered synth patch that was
    // used earlier, skipping applyInstrument leaves the looper keyboard on
    // the wrong engine and the chosen sound appears not to play.
    if (instr >= 0 && (instr != proc.getCurrentInstrument() || ! proc.isSynthMode()))
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
        t.moreButton.setVisible (on);
        t.rerecButton.setVisible (false);
        t.undoButton.setVisible (false);
        t.clearButton.setVisible (false);
        t.muteButton.setVisible (false);
        t.revButton.setVisible (false);
        t.panSlider.setVisible (on);
        t.volSlider.setVisible (on);
    }
    addTrackButton.setEnabled (visibleTracks < LoopStation::kNumTracks);
}

void LooperPanel::timerCallback()
{
    const auto transportLabel = proc.getLooper().anyRunning() ? juce::String ("Stop")
                                                               : juce::String ("Play all");
    if (transportButton.getButtonText() != transportLabel)
        transportButton.setButtonText (transportLabel);

    // Processor owns tempo sync, including while this editor is closed.
    proc.getLooper().collectRetired();
    syncButton.setToggleState (proc.getLooper().isTempoSync(), juce::dontSendNotification);
    bpmSlider.setValue (proc.getLooper().getMetroBpm(), juce::dontSendNotification);
    bpmSlider.setEnabled (! proc.getLooper().isTempoSync() || proc.getHostBpm() <= 0.0);

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
        bool takeInProgress = false;
        const auto& looper = proc.getLooper();
        for (int tr = 0; tr < LoopStation::kNumTracks; ++tr)
        {
            const int state = looper.getTrackState (tr);
            if (state == LoopStation::Recording || state == LoopStation::Overdub)
                takeInProgress = true;
        }

        // A sound selected while another take was live is intentionally
        // deferred until it is safe to replace the processor's global patch.
        const int current = proc.getCurrentInstrument();
        if (! takeInProgress && trackUI[selectedTrack].chosenInstrument >= 0
            && trackUI[selectedTrack].chosenInstrument != current)
            applyTrackInstrument (selectedTrack);

        const int sel    = instrumentBox.getSelectedId();
        const int selIdx = sel >= 1000 ? sel - 1000 : sel - 1;
        const int activeSound = takeInProgress && trackUI[selectedTrack].chosenInstrument >= 0
                              ? trackUI[selectedTrack].chosenInstrument
                              : proc.getCurrentInstrument();
        if (selIdx != activeSound)
            instrumentBox.setSelectedId (activeSound + 1,
                                         juce::dontSendNotification);

        // If the user picked a patch elsewhere in the instrument browser,
        // keep the active track's picker and its next-take sound in sync.
        const int mirroredSound = takeInProgress && trackUI[selectedTrack].chosenInstrument >= 0
                                ? trackUI[selectedTrack].chosenInstrument : current;
        if (! takeInProgress
            && ! trackUI[selectedTrack].usesCurrentChop
            && mirroredSound >= 0
            && mirroredSound < VocalChopAudioProcessor::getInstrumentNames().size()
            && trackUI[selectedTrack].chosenInstrument != mirroredSound)
        {
            trackUI[selectedTrack].chosenInstrument = mirroredSound;
            syncingInstrumentPickers = true;
            trackUI[selectedTrack].instBox.setSelectedId (mirroredSound + 1,
                                                            juce::dontSendNotification);
            syncingInstrumentPickers = false;
        }
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
        // Finishing a take now leaves that lane playing. Move the top picker
        // to the next empty lane so the next record click is immediate, while
        // all completed lanes continue to run underneath it.
        if (lastTrackStates[(size_t) i] == LoopStation::Recording
            && st == LoopStation::Playing)
        {
            for (int step = 1; step <= LoopStation::kNumTracks; ++step)
            {
                const int next = (i + step) % LoopStation::kNumTracks;
                if (looper.getTrackState (next) == LoopStation::Empty)
                {
                    selectedTrack = next;
                    syncingInstrumentPickers = true;
                    instrumentBox.setSelectedId (trackUI[next].usesCurrentChop
                                                     ? 0 : trackUI[next].chosenInstrument + 1,
                                                 juce::dontSendNotification);
                    syncingInstrumentPickers = false;
                    break;
                }
            }
        }
        lastTrackStates[(size_t) i] = st;
        // Sentence case, and never longer than the pad is wide - resized()
        // sizes the pad from exactly this set of words, so "Overdub" (which
        // used to be cut in half on the pad) is now the three-letter "Dub"
        // the ring itself already used.
        switch (st)
        {
            case LoopStation::Empty:     t.mainButton.setButtonText ("Record");  break;
            case LoopStation::Recording: t.mainButton.setButtonText ("Finish");  break;
            case LoopStation::Playing:   t.mainButton.setButtonText ("Dub");  break;
            case LoopStation::Overdub:   t.mainButton.setButtonText ("Play"); break;
            case LoopStation::Stopped:   t.mainButton.setButtonText ("Resume");   break;
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
        if (st == LoopStation::Recording && ! t.waveArea.isEmpty())
            repaint (t.waveArea);
    }
}

void LooperPanel::importAudioToTrack (int track)
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load audio into loop track " + juce::String (track + 1),
        juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
        [safe=juce::Component::SafePointer<LooperPanel>(this), track] (const juce::FileChooser& fc)
    {
        if(safe == nullptr)return;
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        double srcRate = 44100.0;
        auto buf = SampleLoader::decode (file, srcRate);
        if (buf == nullptr
            || ! safe->proc.getLooper().importAudio (track, *buf, srcRate))
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
    auto snapshot = proc.getLooper().captureSnapshot();
    if (! snapshot || ! LoopStation::renderSnapshot (*snapshot, mix))
        return {};

    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getNonexistentChildFile ("Slyce-loop", ".wav", true);

    juce::WavAudioFormat wav;
    auto stream = f.createOutputStream();
    if (stream == nullptr)
        return {};

    if (auto* writer = wav.createWriterFor (stream.get(), snapshot->getSampleRate(),
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

        place (actionsButton, 42);
        head.removeFromRight (6);
        place (transportButton, wideEnough ("Play all", 72));
        head.removeFromRight (12);
        place (tapButton,      wideEnough ("Tap", 46));
        head.removeFromRight (6);
        place (metroButton,    wideEnough ("Click", 58));
        head.removeFromRight (6);
        place (midiButton,     wideEnough ("MIDI", 54));
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
        bpmValueArea = head.removeFromRight (textWidthFor (numFont, "240") + 12);
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

    // --- Footer: one obvious DAW drag target. Export lives in Actions.
    {
        auto footer = area.removeFromBottom (tight ? 44 : 52);
        exportButton.setBounds (juce::Rectangle<int>());
        dragButton.setBounds (footer.reduced (0, 1));
        addTrackButton.setBounds (juce::Rectangle<int>());
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
    const int padInset = juce::jmax (3, rowH / 8);
    const int padBtnH  = juce::jmax (12, rowH - padInset * 2);
    const juce::Font padFont = buttonFontFor (padBtnH);
    int padTextW = 0;
    for (auto* s : kPadLabels)
        padTextW = juce::jmax (padTextW, textWidthFor (padFont, s));
    const int padW = juce::jmax (58, juce::jmax (rowH - 6, padTextW + padInset * 2 + 12));

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

        // Right-hand controls first, so the waveform takes whatever is left.
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
        const int ctlH = juce::jmax (14, juce::jmin (26, rowH - 10));
        t.moreButton.setBounds (row.removeFromRight (42)
                                    .withSizeKeepingCentre (36, ctlH));
        row.removeFromRight (8);

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
        // accTxt, not raw accent: this is the one NUMBER in the panel and the
        // bare accent falls under 4.5:1 on six skins.
        g.setColour (theme.accTxt);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                  bpmValueArea.getHeight() >= 44 ? 20.0f : 17.0f,
                                                  juce::Font::plain)));
        g.drawText (juce::String (juce::roundToInt (proc.getLooper().getMetroBpm())), bpmValueArea,
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
          : st == LoopStation::Recording
                ? juce::String (juce::jmax (0, (int) std::ceil (
                    (1.0f - posN) * looper.getMaxRecordSeconds()))) + "s"
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

        if (st == LoopStation::Recording && t.waveArea.getWidth() >= 40)
        {
            const int secondsLeft = juce::jmax (0, (int) std::ceil (
                (1.0f - posN) * looper.getMaxRecordSeconds()));
            g.setColour (theme.accentInk);
            g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
            g.drawText ("REC - " + juce::String (secondsLeft) + "s left / 30s max",
                        t.waveArea.reduced (8, 0), juce::Justification::centredRight, false);
        }
    }

    // --- How-to line ---------------------------------------------------------
    // ASCII only, and the longest wording that fits: this build renders UTF-8
    // literals as Latin-1, and drawText would otherwise clip mid-word.
    if (! howToArea.isEmpty())
    {
        const juce::Font hintFont (juce::FontOptions (12.0f));
        juce::String hint = "Record up to 30 seconds per track. Set stops; Go plays.   "
                            "Undo flips to Redo.   Tap sets tempo.";
        if (textWidthFor (hintFont, hint) > howToArea.getWidth())
            hint = "30 seconds max per track. Set stops; Go plays.";
        if (textWidthFor (hintFont, hint) > howToArea.getWidth())
            hint = "30s max / track. Set stops; Go plays.";

        g.setColour (theme.textSecondary);
        g.setFont (hintFont);
        g.drawText (hint, howToArea, juce::Justification::centred, false);
    }
}
