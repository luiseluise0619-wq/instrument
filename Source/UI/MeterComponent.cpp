#include "MeterComponent.h"

MeterComponent::MeterComponent (std::atomic<float>& levelSource)
    : level (levelSource)
{
    startTimerHz (60);   // meters must feel instantaneous
}

MeterComponent::~MeterComponent()
{
    stopTimer();
}

void MeterComponent::timerCallback()
{
    // Consume the processor's peak latch (exchange-to-zero): whatever peak
    // happened since the last frame shows up on THIS frame.
    const float target = juce::jlimit (0.0f, 1.0f,
                                       level.exchange (0.0f, std::memory_order_relaxed));

    // Instant attack: jump up immediately. Musical release: ease down.
    if (target >= displayed)
        displayed = target;
    else
        displayed += (target - displayed) * 0.16f;   // per-frame at 60 Hz

    // Peak-hold catches the top and then falls slowly.
    if (displayed >= peakHold)
        peakHold = displayed;
    else
        peakHold -= 0.006f;                            // slow linear fall

    peakHold = juce::jlimit (0.0f, 1.0f, peakHold);

    repaint();
}

void MeterComponent::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();

    auto bounds = getLocalBounds().toFloat();
    if (bounds.getWidth() < 2.0f || bounds.getHeight() < 2.0f)
        return;   // guard against zero-size bounds

    const float radius = theme.cornerRadius;

    // Card region inset slightly so the drop shadow has room to breathe.
    const auto card = bounds.reduced (2.0f);

    // Soft drop shadow beneath the material card.
    {
        juce::Path shadowPath;
        shadowPath.addRoundedRectangle (card, radius);
        juce::DropShadow (theme.shadow, 10, { 0, 2 }).drawForPath (g, shadowPath);
    }

    const bool glowTheme = theme.glow >= 0.9f;

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
        // Material fill with a gentle top-to-bottom vibrancy gradient.
        juce::ColourGradient fill (theme.materialStrong, card.getX(), card.getY(),
                                   theme.material,       card.getX(), card.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (card, radius);

        // 1px hairline border.
        g.setColour (theme.separator);
        g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);
    }

    // Interior padding for the bar + caption strip.
    const float pad = 12.0f;
    auto inner = card.reduced (pad);
    if (inner.getWidth() < 1.0f || inner.getHeight() < 1.0f)
        return;

    // Reserve a small strip at the bottom for the "OUT" caption.
    const float captionH = juce::jmin (16.0f, inner.getHeight() * 0.25f);
    auto captionArea = inner.removeFromBottom (captionH);
    inner.removeFromBottom (4.0f);   // gap between bar and caption

    auto barArea = inner;
    if (barArea.getWidth() < 1.0f || barArea.getHeight() < 1.0f)
        return;

    const float barRadius = juce::jmin (4.0f, barArea.getWidth() * 0.5f);

    // Track (inactive) background for the bar.
    g.setColour (theme.controlTrack);
    g.fillRoundedRectangle (barArea, barRadius);

    // Warning tint that caps the very top of the scale.
    const juce::Colour warn (0xffff453a);

    // Faint tick marks every 25% of the scale, etched into the track.
    g.setColour (theme.separator);
    for (int i = 1; i < 4; ++i)
    {
        const float tickY = barArea.getBottom() - barArea.getHeight() * (0.25f * (float) i);
        g.fillRect (juce::Rectangle<float> (barArea.getX(), tickY - 0.5f,
                                            barArea.getWidth(), 1.0f));
    }

    // Active fill from the bottom, height proportional to displayed level.
    const float level01 = juce::jlimit (0.0f, 1.0f, displayed);
    if (level01 > 0.0f)
    {
        const float fillH = barArea.getHeight() * level01;
        auto fillRect = barArea.withTop (barArea.getBottom() - fillH);

        // Soft bloom behind the lit portion on glow themes.
        if (theme.glow > 0.0f)
        {
            g.setColour (theme.accent.withAlpha (0.16f * theme.glow));
            g.fillRoundedRectangle (fillRect.expanded (2.5f), barRadius + 2.5f);
        }

        // When levels run hot, the bar radiates a faint wider neon halo
        // shifting from cyan toward hot pink.
        if (glowTheme)
        {
            const float heat = juce::jlimit (0.0f, 1.0f, (level01 - 0.55f) / 0.35f);
            if (heat > 0.0f)
            {
                const juce::Colour hot = theme.accent.interpolatedWith (theme.waveform, heat);
                for (int layer = 3; layer >= 1; --layer)
                {
                    g.setColour (hot.withAlpha (heat * 0.07f * (float) (4 - layer)));
                    g.fillRoundedRectangle (fillRect.expanded (2.0f + 2.5f * (float) layer),
                                            barRadius + 2.0f + 2.5f * (float) layer);
                }
            }
        }

        // Vertical gradient pinned to the full scale so the fill "reveals" it:
        // accent at the bottom, hot (waveform) near the top, red at the peak.
        juce::ColourGradient grad (theme.accent, barArea.getX(), barArea.getBottom(),
                                   warn,         barArea.getX(), barArea.getY(), false);
        grad.addColour (0.80, theme.waveform);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fillRect, barRadius);
    }

    // Peak-hold marker: a thin line at the held level.
    if (peakHold > 0.0f)
    {
        const float peakY = barArea.getBottom() - barArea.getHeight() * juce::jlimit (0.0f, 1.0f, peakHold);

        if (glowTheme)
        {
            // Neon pink peak-hold tick with a soft bloom.
            g.setColour (theme.waveform.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (barArea.getX() - 1.0f, peakY - 3.5f,
                                                barArea.getWidth() + 2.0f, 7.0f));
            g.setColour (theme.waveform.withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> (barArea.getX(), peakY - 2.0f,
                                                barArea.getWidth(), 4.0f));
            g.setColour (theme.waveform.brighter (0.25f));
            g.fillRect (juce::Rectangle<float> (barArea.getX(), peakY - 1.0f,
                                                barArea.getWidth(), 2.0f));
        }
        else
        {
            const juce::Colour peakColour = theme.accent.interpolatedWith (warn, peakHold * peakHold);

            g.setColour (peakColour.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (barArea.getX(), peakY - 1.0f, barArea.getWidth(), 2.0f));
        }
    }

    // "OUT" caption in the secondary text colour.
    if (captionArea.getHeight() >= 8.0f)
    {
        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Semibold")));
        g.drawText ("OUT", captionArea, juce::Justification::centred, false);
    }
}
