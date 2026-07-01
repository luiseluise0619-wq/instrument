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

    const auto  bounds = getLocalBounds().toFloat();
    const float radius = theme.cornerRadius;

    // Card region inset slightly so the drop shadow has room to breathe.
    const auto card = bounds.reduced (2.0f);

    // Soft drop shadow beneath the material card.
    {
        juce::Path shadowPath;
        shadowPath.addRoundedRectangle (card, radius);
        juce::DropShadow (theme.shadow, 10, { 0, 2 }).drawForPath (g, shadowPath);
    }

    // Material fill with a gentle top-to-bottom vibrancy gradient.
    juce::ColourGradient fill (theme.materialStrong, card.getX(), card.getY(),
                               theme.material,       card.getX(), card.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (card, radius);

    // 1px hairline border.
    g.setColour (theme.separator);
    g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);

    // Uppercase, tracked title "FX" in the top padding strip.
    {
        const float pad = 16.0f;
        auto titleArea = card.reduced (pad, 0.0f).withTop (card.getY() + 12.0f).withHeight (16.0f);

        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Semibold")));

        // Draw glyph-by-glyph to add wide letter-spacing (tracking).
        const juce::String title ("FX");
        const float tracking = 3.0f;
        const auto& font = g.getCurrentFont();

        float x = titleArea.getX();
        const float cy = titleArea.getCentreY();
        for (auto ch : title)
        {
            const juce::String s = juce::String::charToString (ch);
            const float w = font.getStringWidthFloat (s);
            g.drawText (s,
                        juce::Rectangle<float> (x, cy - 8.0f, w + tracking, 16.0f),
                        juce::Justification::centredLeft, false);
            x += w + tracking;
        }
    }

    // Hairline dividers between the stacked modules.
    if (modules.size() > 1)
    {
        const float pad = 16.0f;
        g.setColour (theme.separator);

        for (size_t i = 1; i < modules.size(); ++i)
        {
            const auto b = modules[i].knob->getBounds().toFloat();
            const float y = b.getY() - (moduleGap * 0.5f);
            g.fillRect (juce::Rectangle<float> (card.getX() + pad, y,
                                                card.getWidth() - pad * 2.0f, 1.0f));
        }
    }
}

void FXRack::resized()
{
    auto area = getLocalBounds().reduced (2);   // match the card inset
    area = area.reduced (16);                    // generous interior padding
    area.removeFromTop (titleStrip);             // reserve the title strip

    if (modules.empty())
        return;

    const int count       = (int) modules.size();
    const int totalGaps    = (count - 1) * moduleGap;
    const int slotHeight   = (area.getHeight() - totalGaps) / count;

    for (int i = 0; i < count; ++i)
    {
        auto slot = area.removeFromTop (slotHeight);
        modules[(size_t) i].knob->setBounds (slot);

        if (i < count - 1)
            area.removeFromTop (moduleGap);
    }
}
