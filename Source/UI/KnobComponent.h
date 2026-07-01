#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Apple / macOS-style rotary knob.

    A refined, flat rotary control: soft drop shadow, a base disc, a thin
    inactive track ring, an accent progress arc with rounded caps, and a small
    crisp indicator near the rim. A caption label sits below the dial. Wraps a
    juce::Slider so it plugs straight into an
    AudioProcessorValueTreeState::SliderAttachment.

    All colours are pulled live from ThemeManager::active() so switching themes
    restyles the control instantly.
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
