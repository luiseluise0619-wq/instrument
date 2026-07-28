#include "ScopePanel.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

#include <cmath>

namespace
{
    /** Spec 2: derived accent tokens are computed, never hand-picked.
        --acc2 = color-mix(in srgb, var(--acc) 76%, #000) -> 24% toward black.
        Same helper the waveform uses for its bar tips. */
    juce::Colour accentDeep (const Theme& t)
    {
        return t.accent.interpolatedWith (juce::Colours::black, 0.24f);
    }

    // Per-frame ballistics at 30 Hz. Attack is instant so a transient never
    // gets swallowed; release is eased so the row settles instead of flickering.
    constexpr float kRelease = 0.34f;

    // Below this the bar counts as silent: no accent is drawn at all (spec 1 -
    // accent means "currently active", it is never decoration).
    constexpr float kSilence = 0.005f;
}

ScopePanel::ScopePanel (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    // No timer here on purpose: a fresh Component is not visible yet, so
    // visibilityChanged() starts the clock the moment the editor shows us.
}

ScopePanel::~ScopePanel()
{
    stopTimer();
}

void ScopePanel::visibilityChanged()
{
    if (isVisible())
    {
        if (! isTimerRunning())
        {
            lastScopePos = -1;      // re-sync to "now" instead of replaying history
            startTimerHz (30);
        }
    }
    else
    {
        stopTimer();
    }
}

void ScopePanel::timerCallback()
{
    // A visible child of a hidden parent still gets timer ticks: do no work.
    if (! isShowing())
        return;

    // --- newest sample block from the processor's scope ring ---------------
    // Read-only and non-consuming, so this cannot steal peaks from the meter.
    const auto& ring     = proc.getScopeRing();
    const int   ringSize = (int) ring.size();
    const int   writePos = proc.getScopeWritePos();

    if (lastScopePos < 0)
        lastScopePos = writePos;    // first frame after becoming visible

    int count = writePos - lastScopePos;
    if (count < 0)                  // the processor's counter wrapped
        count = 0;
    count = juce::jmin (count, ringSize);
    lastScopePos = writePos;

    float peak = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const float s = std::fabs (ring[(size_t) ((writePos - 1 - i) & (ringSize - 1))]);
        if (s > peak)
            peak = s;
    }

    peak = juce::jlimit (0.0f, 1.0f, peak);

    // --- scroll the history one bar to the left ----------------------------
    // shown[] scrolls with target[] so every bar keeps its own ballistic
    // state, and the new right-most bar starts from where "now" just was.
    for (int i = 0; i < kNumBars - 1; ++i)
    {
        target[(size_t) i] = target[(size_t) (i + 1)];
        shown [(size_t) i] = shown [(size_t) (i + 1)];
    }

    target[(size_t) (kNumBars - 1)] = peak;

    // --- per-bar ballistics: instant attack, eased release -----------------
    float peakShown = 0.0f;
    for (int i = 0; i < kNumBars; ++i)
    {
        const auto k = (size_t) i;

        if (target[k] >= shown[k])
            shown[k] = target[k];
        else
            shown[k] += (target[k] - shown[k]) * kRelease;

        peakShown = juce::jmax (peakShown, shown[k]);
    }

    // An idle scope must not burn CPU: once every bar has settled to silence
    // and the last frame drew that too, there is nothing new to show.
    const bool idle = peakShown < 0.002f && lastDrawnPeak < 0.002f;
    if (! idle)
    {
        lastDrawnPeak = peakShown;
        repaint();
    }
}

void ScopePanel::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();

    auto bounds = getLocalBounds().toFloat();
    if (bounds.getWidth() < 2.0f || bounds.getHeight() < 2.0f)
        return;   // guard against zero-size bounds

    const bool glowTheme = theme.glow >= 0.9f;

    // Sunken well only - the card, its border and the caption are drawn by the
    // editor. theme.shadow keeps the inset reading correctly on light themes,
    // where a flat dark fill would look like a hole.
    const float wellRadius = juce::jmin (theme.cornerRadius * 0.75f, bounds.getHeight() * 0.5f);
    const float wellAlpha  = glowTheme ? 0.42f : (theme.dark ? 0.30f : 0.10f);

    g.setColour (theme.shadow.withAlpha (wellAlpha));
    g.fillRoundedRectangle (bounds, wellRadius);

    if (glowTheme)
    {
        // A whisper of accent inside the glass, the way the neon cards read.
        g.setColour (theme.accent.withAlpha (0.05f));
        g.fillRoundedRectangle (bounds, wellRadius);
    }

    // --- bar geometry ------------------------------------------------------
    const float padX = juce::jlimit (2.0f, 8.0f, bounds.getWidth()  * 0.05f);
    const float padY = juce::jlimit (2.0f, 8.0f, bounds.getHeight() * 0.10f);

    auto inner = bounds.reduced (padX, padY);
    if (inner.getWidth() < (float) kNumBars || inner.getHeight() < 2.0f)
        return;

    const float slot = inner.getWidth() / (float) kNumBars;
    const float gap  = juce::jlimit (1.0f, 3.0f, slot * 0.30f);
    const float barW = juce::jmax (1.0f, slot - gap);
    const float capR = barW * 0.5f;          // fully rounded caps

    const juce::Colour deep = accentDeep (theme);

    for (int i = 0; i < kNumBars; ++i)
    {
        const bool  newest = (i == kNumBars - 1);
        const float x      = inner.getX() + slot * (float) i + (slot - barW) * 0.5f;

        const juce::Rectangle<float> track (x, inner.getY(), barW, inner.getHeight());

        // Age ramp: the oldest bar sits back, "now" is at full strength.
        const float age      = (float) i / (float) (kNumBars - 1);
        const float ageAlpha = 0.45f + 0.55f * age;

        // Inactive track pill - the same token the output meter uses.
        g.setColour (theme.controlTrack.withMultipliedAlpha (newest ? 1.0f : 0.7f));
        g.fillRoundedRectangle (track, capR);

        const float v = juce::jlimit (0.0f, 1.0f, shown[(size_t) i]);
        if (v <= kSilence)
            continue;

        // Fill from the bottom, never thinner than one cap so a quiet bar is
        // still a bar and not a sliver.
        const float fillH = juce::jmax (juce::jmin (barW, inner.getHeight()),
                                        inner.getHeight() * v);
        const auto  bar   = track.withTop (track.getBottom() - fillH);

        // Soft bloom behind the lit portion on glow themes.
        if (theme.glow > 0.0f)
        {
            g.setColour (theme.accent.withAlpha (0.15f * theme.glow * ageAlpha));
            g.fillRoundedRectangle (bar.expanded (2.0f), capR + 2.0f);
        }

        if (newest && glowTheme)
        {
            // "Now" radiates a wider halo, matching the meter's hot bloom.
            for (int layer = 2; layer >= 1; --layer)
            {
                g.setColour (theme.accent.withAlpha (0.07f * (float) (3 - layer)));
                g.fillRoundedRectangle (bar.expanded (2.0f + 2.5f * (float) layer),
                                        capR + 2.0f + 2.5f * (float) layer);
            }
        }

        // Accent at the base, the derived deep accent at the tip (spec 4.3).
        juce::ColourGradient grad (theme.accent.withMultipliedAlpha (ageAlpha),
                                   track.getX(), track.getBottom(),
                                   deep.withMultipliedAlpha (ageAlpha),
                                   track.getX(), track.getY(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar, capR);

        if (newest)
        {
            // Emphasis on the live bar: a bright cap that pulls away from the
            // well in both light and dark themes.
            const auto cap = bar.withHeight (juce::jmin (bar.getHeight(), barW));

            g.setColour (theme.dark ? theme.accent.brighter (0.45f)
                                    : theme.accent.darker (0.20f));
            g.fillRoundedRectangle (cap, capR);
        }
    }
}
