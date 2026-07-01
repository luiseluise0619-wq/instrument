#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class VocalChopAudioProcessor;

/**
    Grid of trigger pads, one per slice. Clicking a pad plays that slice
    through the processor's voice pool. Pads reflow to a roughly square grid
    and flash on trigger.
*/
class SliceGrid : public juce::Component,
                  private juce::Timer
{
public:
    explicit SliceGrid (VocalChopAudioProcessor& processor);
    ~SliceGrid() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Call after slices change to refresh the pad layout. */
    void refresh() { repaint(); }

private:
    void timerCallback() override;
    int  padIndexAt (juce::Point<int> position) const;
    juce::Rectangle<float> padBounds (int index, int numPads) const;

    VocalChopAudioProcessor& proc;
    std::vector<float> padFlash;   // per-pad decay level
    int lastPadCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceGrid)
};
