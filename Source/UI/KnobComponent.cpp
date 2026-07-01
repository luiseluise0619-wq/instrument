#include "KnobComponent.h"
#include "ThemeManager.h"

//==============================================================================
void KnobComponent::KnobLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& slider)
{
    const auto& theme = ThemeManager::active();

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Drop shadow.
    {
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.6f), 12, { 0, 4 });
        juce::Path body;
        body.addEllipse (bounds);
        shadow.drawForPath (g, body);
    }

    // Glass body gradient.
    juce::ColourGradient grad (theme.knob.brighter (0.3f), centre.x, bounds.getY(),
                               theme.knob.darker (0.5f),  centre.x, bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillEllipse (bounds);

    // Rim glow.
    g.setColour (theme.accent.withAlpha (0.35f * theme.glow));
    g.drawEllipse (bounds, 2.0f);

    // Value arc.
    juce::Path arc;
    const float arcRadius = radius + 3.0f;
    arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                       rotaryStartAngle, angle, true);
    g.setColour (theme.accent);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Pointer.
    juce::Path pointer;
    const float pointerLength = radius * 0.7f;
    const float pointerThickness = 3.0f;
    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -radius + 4.0f,
                                 pointerThickness, pointerLength, pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (theme.highlight);
    g.fillPath (pointer);

    // Centre cap.
    g.setColour (theme.knob.darker (0.2f));
    g.fillEllipse (juce::Rectangle<float> (0, 0, radius * 0.4f, radius * 0.4f).withCentre (centre));
}

//==============================================================================
KnobComponent::KnobComponent (const juce::String& caption)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    slider.setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (slider);

    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

KnobComponent::~KnobComponent()
{
    slider.setLookAndFeel (nullptr);
}

void KnobComponent::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromBottom (18));
    slider.setBounds (area);
}

void KnobComponent::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    label.setColour (juce::Label::textColourId, theme.text);
    slider.setColour (juce::Slider::textBoxTextColourId, theme.text);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    g.setColour (theme.text);
}
