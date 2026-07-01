#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "KnobComponent.h"
#include <memory>
#include <vector>

/**
    Vertical rack of FX modules (Drive / Reverb / Delay), each a captioned knob
    bound to its parameter. The processing order in the audio engine is fixed
    (distortion → reverb → delay); this rack is the UI for their amounts.
*/
class FXRack : public juce::Component
{
public:
    explicit FXRack (juce::AudioProcessorValueTreeState& apvts);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Module
    {
        std::unique_ptr<KnobComponent> knob;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addModule (juce::AudioProcessorValueTreeState& apvts,
                    const juce::String& paramID, const juce::String& caption);

    std::vector<Module> modules;

    // Layout metrics (kept in sync between resized() and paint()).
    static constexpr int titleStrip = 26; // reserved height for the "FX" title
    static constexpr int moduleGap  = 16; // vertical space between modules / divider

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXRack)
};
