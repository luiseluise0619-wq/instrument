#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class VocalChopAudioProcessor;

/**
    The LOOPER tab: an RC-505-style layer looper for the plugin's output.

    One big main pad (record -> set length -> overdub/play), Stop and Clear,
    a loop-volume slider, a progress ring and layer counter. The on-screen
    keyboard below stays live, so a whole beat can be stacked from one
    laptop: record a chop groove, overdub the 808, overdub hats...
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

    juce::TextButton mainButton  { "REC" };
    juce::TextButton stopButton  { "STOP" };
    juce::TextButton clearButton { "CLEAR" };
    juce::Slider     volumeSlider;
    juce::Label      volumeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperPanel)
};
