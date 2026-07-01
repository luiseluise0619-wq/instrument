#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class VocalChopAudioProcessor;

/**
    Draws the loaded sample as a filled waveform with slice boundary markers,
    a subtle animated glow, and accepts drag-and-drop of audio files to load a
    new sample. Clicking rebuilds slices via the processor's SliceEngine.
*/
class WaveformView : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
{
public:
    explicit WaveformView (VocalChopAudioProcessor& processor);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Recomputes the cached min/max envelope from the current sample. */
    void refresh();

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void rebuildEnvelope();

    VocalChopAudioProcessor& proc;
    std::vector<float> minEnv, maxEnv;
    float glowPhase = 0.0f;
    bool  fileHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
