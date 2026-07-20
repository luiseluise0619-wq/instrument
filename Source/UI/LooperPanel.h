#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../AudioEngine/LoopStation.h"
#include <memory>

class VocalChopAudioProcessor;

/**
    The LOOPER tab: an RC-505-style multi-track looper for the plugin's
    output (4 tracks visible, expandable to 6 with + TRACK).

    Each track has its own instrument picker (chosen BEFORE recording, so a
    tap on REC drops you straight into the right sound), one big pad
    (record -> set length -> overdub/play), RE-record, UNDO for the last
    dub pass, mute, clear and volume, plus a progress ring. Track 1 defines
    the loop length; later tracks quantise to a multiple of it. A BPM
    metronome click (never recorded) keeps takes honest. The on-screen
    keyboard below stays live the whole time.
*/
class LooperPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit LooperPanel (VocalChopAudioProcessor& processor);
    ~LooperPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void populateInstrumentBox (juce::ComboBox&, bool withQuickShelf);
    void applyTrackInstrument (int track);
    void updateTrackVisibility();
    void importAudioToTrack (int track);   // file -> loop (combo's top entry)

    static constexpr int kLoadAudioId = 900000;   // per-track combo item id

    VocalChopAudioProcessor& proc;

    struct TrackUI
    {
        juce::ComboBox   instBox;        // this track's pre-picked sound
        juce::TextButton mainButton  { "REC" };
        juce::TextButton rerecButton { "RE" };
        juce::TextButton undoButton  { "UNDO" };
        juce::TextButton clearButton { "X" };
        juce::TextButton muteButton  { "M" };
        juce::TextButton revButton   { "REV" };
        juce::Slider     panSlider;
        juce::Slider     volSlider;
        juce::Rectangle<int> ringArea;   // painted by the panel
        int chosenInstrument = -1;       // -1 = keep whatever is loaded
    };
    TrackUI trackUI[LoopStation::kNumTracks];
    int visibleTracks = 4;

    juce::TextButton playAllButton  { "PLAY ALL" };
    juce::TextButton stopAllButton  { "STOP ALL" };
    juce::TextButton clearAllButton { "CLEAR ALL" };
    juce::TextButton addTrackButton { "+ TRACK" };
    juce::TextButton metroButton    { "MET" };
    juce::TextButton tapButton      { "TAP" };
    juce::Slider     bpmSlider;
    double lastTapMs = 0.0;
    double tapIntervalMs = 0.0;
    int    tapCount = 0;

    // Pick the CURRENT sound without leaving the looper.
    juce::ComboBox engineBox;      // Chop / Synth
    juce::ComboBox instrumentBox;  // Featured + category submenus
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineAttachment;
    std::unique_ptr<juce::FileChooser> fileChooser;   // per-track audio import

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPanel)
};
