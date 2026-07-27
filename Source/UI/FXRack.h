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

    /** The three FX dials by name, so the macros can light the ones they
        drive. Returns nullptr if the module is missing rather than asserting -
        a macro pointing at a dial that does not exist should do nothing, not
        crash. */
    KnobComponent* driveKnob()  const { return knobAt (0); }
    KnobComponent* reverbKnob() const { return knobAt (1); }
    KnobComponent* delayKnob()  const { return knobAt (2); }

private:
    KnobComponent* knobAt (int i) const
    {
        return juce::isPositiveAndBelow (i, (int) modules.size())
                 ? modules[(size_t) i].knob.get() : nullptr;
    }

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
