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
class KnobComponent : public juce::Component,
                      private juce::Timer,
                      private juce::Slider::Listener
{
public:
    explicit KnobComponent (const juce::String& caption);
    ~KnobComponent() override;

    juce::Slider& getSlider() { return slider; }

    void resized() override;

    /** A second line under the caption, for dials whose name does not say what
        they do. Empty by default, and the layout only reserves space when one
        is set - so no other dial in the window moves. */
    void setSubCaption (const juce::String& text);

    /** Lights the dial without the mouse being on it. Used by the macros to
        show, on hover, which parameters they actually move - a macro is a
        shortcut whose whole problem is that you cannot see what it does. */
    void setLearnGlow (bool on);

    /** Fires on mouse enter/exit so an owner can light other dials. */
    std::function<void (bool)> onHoverChanged;
    void paint (juce::Graphics&) override;

private:
    class KnobLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;

    };

    // Light-trail: turning the knob flares the glow, which then decays.
    // (Slider::Listener is used so clients remain free to set slider.onValueChange.)
    void sliderValueChanged (juce::Slider*) override;
    void timerCallback() override;

    struct HoverWatcher : juce::MouseListener
    {
        explicit HoverWatcher (KnobComponent& o) : owner (o) {}
        void mouseEnter (const juce::MouseEvent&) override
        { if (owner.onHoverChanged) owner.onHoverChanged (true); }
        void mouseExit (const juce::MouseEvent&) override
        { if (owner.onHoverChanged) owner.onHoverChanged (false); }
        KnobComponent& owner;
    };
    HoverWatcher hoverWatcher { *this };

    juce::Slider slider;
    juce::Label  label, subLabel;
    KnobLookAndFeel lookAndFeel;
    float dragGlow = 0.0f;
    bool  learnGlow = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobComponent)
};
