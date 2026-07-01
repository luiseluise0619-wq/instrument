#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Rotary knob with a soft-shadow / glow look pulled from the active theme,
    plus a caption label underneath. Wrap a Slider so it plugs straight into
    an AudioProcessorValueTreeState::SliderAttachment.
*/
class KnobComponent : public juce::Component
{
public:
    explicit KnobComponent (const juce::String& caption);
    ~KnobComponent() override;

    juce::Slider& getSlider() { return slider; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class KnobLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;
    };

    juce::Slider slider;
    juce::Label  label;
    KnobLookAndFeel lookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobComponent)
};
