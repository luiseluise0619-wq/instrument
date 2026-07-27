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

    // Keep the rack's neon value tracks in sync while the knob moves.
    // (KnobComponent leaves onValueChange free for clients; attachments use
    // Slider::Listener, so this callback is ours.)
    m.knob->getSlider().onValueChange = [this]
    {
        if (ThemeManager::active().glow >= 0.9f)
            repaint();
    };

    modules.push_back (std::move (m));
}

void FXRack::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    const bool  glowTheme = theme.glow >= 0.9f;

    const auto  bounds = getLocalBounds().toFloat();
    const float radius = theme.cornerRadius;

    // Card region inset slightly so the drop shadow has room to breathe.
    const auto card = bounds.reduced (2.0f);

    // Soft drop shadow beneath the material card (cheap offset fills - this
    // rack repaints whenever an FX knob moves).
    g.setColour (theme.shadow.withAlpha (0.16f));
    g.fillRoundedRectangle (card.translated (0.0f, 4.0f).expanded (1.5f), radius + 1.5f);
    g.setColour (theme.shadow.withAlpha (0.10f));
    g.fillRoundedRectangle (card.translated (0.0f, 2.0f), radius);

    if (glowTheme)
    {
        // Dark glass fill + neon rim, matching the editor's cards.
        g.setColour (juce::Colour (0xc008102a));
        g.fillRoundedRectangle (card, radius);

        g.setColour (theme.accent.withAlpha (0.10f));
        g.drawRoundedRectangle (card.expanded (1.0f), radius + 1.0f, 2.5f);
        g.setColour (theme.accent.withAlpha (0.28f));
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);
    }
    else
    {
        // Flat material fill - the same surface as every other card (the old
        // strong->clear gradient made this one column look broken).
        g.setColour (theme.material);
        g.fillRoundedRectangle (card, radius);

        // 1px hairline border, picking up a whisper of accent on glow themes.
        const juce::Colour borderColour =
            theme.glow > 0.0f ? theme.separator.interpolatedWith (theme.accentSoft, 0.35f * theme.glow)
                              : theme.separator;
        g.setColour (borderColour);
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);
    }

    // Uppercase, tracked title "FX" in the top padding strip.
    {
        const float pad = 16.0f;
        auto titleArea = card.reduced (pad, 0.0f).withTop (card.getY() + 12.0f).withHeight (16.0f);

        const juce::Colour titleColour =
            glowTheme       ? theme.accent
          : theme.glow > 0.0f ? theme.textSecondary.interpolatedWith (theme.accent, 0.30f * theme.glow)
                              : theme.textSecondary;
        g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Semibold")));

        // Accent tick before the caption - matches every other card header.
        g.setColour (theme.accent.withAlpha (0.85f));
        g.fillRoundedRectangle (titleArea.getX(), titleArea.getCentreY() - 5.0f,
                                3.0f, 10.0f, 1.5f);

        // Draw glyph-by-glyph to add wide letter-spacing (tracking).
        const juce::String title ("FX");
        const float tracking = glowTheme ? 2.0f : 1.6f;
        const auto& font = g.getCurrentFont();

        float x = titleArea.getX() + 9.0f;
        const float cy = titleArea.getCentreY();
        for (auto ch : title)
        {
            const juce::String s = juce::String::charToString (ch);
            const float w = juce::GlyphArrangement::getStringWidth (font, s);
            const juce::Rectangle<float> glyphBox (x, cy - 8.0f, w + tracking, 16.0f);

            // Soft neon halo behind each glyph on the cyberpunk theme.
            if (glowTheme)
            {
                static const float dx[] = { -1.2f, 1.2f,  0.0f, 0.0f };
                static const float dy[] = {  0.0f, 0.0f, -1.2f, 1.2f };

                g.setColour (theme.accent.withAlpha (0.22f));
                for (int o = 0; o < 4; ++o)
                    g.drawText (s, glyphBox.translated (dx[o], dy[o]),
                                juce::Justification::centredLeft, false);
            }

            g.setColour (titleColour);
            g.drawText (s, glyphBox, juce::Justification::centredLeft, false);
            x += w + tracking;
        }
    }

    if (glowTheme)
    {
        // Each module gets a slim cyan->purple value track with a small
        // glowing dot at its current position, drawn in the gap below it.
        const float pad = 16.0f;
        const juce::Colour purple (0xffb026ff);

        for (const auto& m : modules)
        {
            const auto kb = m.knob->getBounds().toFloat();
            if (kb.isEmpty())
                continue;

            auto& s = m.knob->getSlider();
            const float pos = juce::jlimit (0.0f, 1.0f,
                                            (float) s.valueToProportionOfLength (s.getValue()));

            const float y = kb.getBottom() + moduleGap * 0.5f;
            const juce::Rectangle<float> track (card.getX() + pad, y - 1.5f,
                                                card.getWidth() - pad * 2.0f, 3.0f);

            // Inactive track base.
            g.setColour (theme.controlTrack);
            g.fillRoundedRectangle (track, 1.5f);

            // Lit portion sweeps cyan -> electric purple, like the knob arcs.
            juce::ColourGradient grad (theme.accent.withAlpha (0.60f), track.getX(),     y,
                                       purple.withAlpha (0.60f),       track.getRight(), y, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (track.withWidth (juce::jmax (3.0f, track.getWidth() * pos)), 1.5f);

            // Small glowing value dot at the current position.
            const juce::Point<float> dot (track.getX() + track.getWidth() * pos, y);
            const juce::Colour dotCol = theme.accent.interpolatedWith (purple, pos);

            g.setColour (dotCol.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (dot));
            g.setColour (dotCol.withAlpha (0.60f));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
            g.setColour (juce::Colours::white.interpolatedWith (dotCol, 0.25f));
            g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre (dot));
        }
    }
    else if (modules.size() > 1)
    {
        // Hairline dividers between the stacked modules.
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
