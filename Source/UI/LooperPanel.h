#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../AudioEngine/LoopStation.h"
#include <memory>

class VocalChopAudioProcessor;

/**
    The LOOPER tab: an RC-505-style 4-track looper for the plugin's output.

    Each track has one big pad (record -> set length -> overdub/play), an
    UNDO for the last dub pass, mute, clear and its own volume, plus a
    progress ring. Track 1 defines the loop length; later tracks quantise
    to a multiple of it. The on-screen keyboard below stays live, so a
    whole beat can be stacked from one laptop: chop groove on T1, 808 on
    T2, hats on T3, vocal hook on T4.
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

    VocalChopAudioProcessor& proc;

    struct TrackUI
    {
        juce::TextButton mainButton  { "REC" };
        juce::TextButton undoButton  { "UNDO" };
        juce::TextButton clearButton { "X" };
        juce::TextButton muteButton  { "M" };
        juce::Slider     volSlider;
        juce::Rectangle<int> ringArea;   // painted by the panel
    };
    TrackUI trackUI[LoopStation::kNumTracks];

    juce::TextButton playAllButton  { "PLAY ALL" };
    juce::TextButton stopAllButton  { "STOP ALL" };
    juce::TextButton clearAllButton { "CLEAR ALL" };

    // Pick the next layer's sound without leaving the looper.
    juce::ComboBox engineBox;      // Chop / Synth
    juce::ComboBox instrumentBox;  // Featured + category submenus
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPanel)
};
