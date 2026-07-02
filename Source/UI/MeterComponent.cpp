#include "MeterComponent.h"

MeterComponent::MeterComponent (std::atomic<float>& levelSource)
    : level (levelSource)
{
    startTimerHz (30);
}

MeterComponent::~MeterComponent()
{
    stopTimer();
}

void MeterComponent::timerCallback()
{
    // Read the external level (clamped to a sane 0..1 range). Read-only.
    const float target = juce::jlimit (0.0f, 1.0f, level.load (std::memory_order_relaxed));

    // Fast attack: jump up immediately. Slow release: ease down toward target.
    if (target >= displayed)
        displayed = target;
    else
        displayed += (target - displayed) * 0.25f;   // exponential ease-down

    // Peak-hold catches the top and then falls slowly.
    if (displayed >= peakHold)
        peakHold = displayed;
    else
        peakHold -= 0.010f;                            // slow linear fall

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

    // Material fill with a gentle top-to-bottom vibrancy gradient.
    juce::ColourGradient fill (theme.materialStrong, card.getX(), card.getY(),
                               theme.material,       card.getX(), card.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (card, radius);

    // 1px hairline border.
    g.setColour (theme.separator);
    g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);

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
        const juce::Colour peakColour = theme.accent.interpolatedWith (warn, peakHold * peakHold);

        g.setColour (peakColour.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (barArea.getX(), peakY - 1.0f, barArea.getWidth(), 2.0f));
    }

    // "OUT" caption in the secondary text colour.
    if (captionArea.getHeight() >= 8.0f)
    {
        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Semibold")));
        g.drawText ("OUT", captionArea, juce::Justification::centred, false);
    }
}
