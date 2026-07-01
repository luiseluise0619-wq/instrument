#include "FXRack.h"
#include "ThemeManager.h"

FXRack::FXRack (juce::AudioProcessorValueTreeState& apvts)
{
    addModule (apvts, "drive",  "Drive");
    addModule (apvts, "reverb", "Reverb");
    addModule (apvts, "delay",  "Delay");
}

void FXRack::addModule (juce::AudioProcessorValueTreeState& apvts,
                        const juce::String& paramID, const juce::String& caption)
{
    Module m;
    m.knob = std::make_unique<KnobComponent> (caption);
    m.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, m.knob->getSlider());
    addAndMakeVisible (*m.knob);
    modules.push_back (std::move (m));
}

void FXRack::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    g.setColour (theme.panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (theme.accent.withAlpha (0.25f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);

    g.setColour (theme.text);
    g.setFont (14.0f);
    g.drawText ("FX RACK", getLocalBounds().removeFromTop (22),
                juce::Justification::centred);
}

void FXRack::resized()
{
    auto area = getLocalBounds().reduced (8);
    area.removeFromTop (18); // title strip

    if (modules.empty())
        return;

    const int knobHeight = area.getHeight() / (int) modules.size();
    for (auto& m : modules)
        m.knob->setBounds (area.removeFromTop (knobHeight).reduced (2));
}
