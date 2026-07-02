#include "SliceGrid.h"
#include "ThemeManager.h"
#include "../PluginProcessor.h"

#include <cmath>

SliceGrid::SliceGrid (VocalChopAudioProcessor& processor)
    : proc (processor)
{
    startTimerHz (60);
}

SliceGrid::~SliceGrid()
{
    stopTimer();
}

//==============================================================================
bool SliceGrid::isBlackKey (int semitone)
{
    const int s = semitone % 12;
    return s == 1 || s == 3 || s == 6 || s == 8 || s == 10;
}

int SliceGrid::keySpan() const
{
    // Whole octaves, at least one, enough to cover every slice.
    const int numSlices = proc.getSliceEngine().getNumSlices();
    const int octaves   = juce::jlimit (1, 3, (numSlices + 11) / 12);
    return octaves * 12;
}

juce::Rectangle<float> SliceGrid::keyboardArea() const
{
    return getLocalBounds().toFloat().reduced (6.0f, 6.0f);
}

juce::Rectangle<float> SliceGrid::keyRect (int semitone, int span) const
{
    const auto area = keyboardArea();

    // Count white keys in the span and this key's white index.
    int whitesTotal = 0, whitesBefore = 0;
    for (int s = 0; s < span; ++s)
    {
        if (! isBlackKey (s))
        {
            if (s < semitone) ++whitesBefore;
            ++whitesTotal;
        }
    }

    const float whiteW = area.getWidth() / (float) juce::jmax (1, whitesTotal);

    if (! isBlackKey (semitone))
        return { area.getX() + whitesBefore * whiteW, area.getY(),
                 whiteW, area.getHeight() };

    // Black key: centred on the boundary after the previous white key.
    const float blackW = whiteW * 0.62f;
    const float x = area.getX() + whitesBefore * whiteW - blackW * 0.5f;
    return { x, area.getY(), blackW, area.getHeight() * 0.62f };
}

int SliceGrid::keyAt (juce::Point<float> p) const
{
    const int span = keySpan();

    // Black keys sit on top, so hit-test them first.
    for (int s = 0; s < span; ++s)
        if (isBlackKey (s) && keyRect (s, span).contains (p))
            return s;
    for (int s = 0; s < span; ++s)
        if (! isBlackKey (s) && keyRect (s, span).contains (p))
            return s;
    return -1;
}

//==============================================================================
void SliceGrid::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();
    const int numSlices = proc.getSliceEngine().getNumSlices();
    const int span = keySpan();

    if ((int) keyFlash.size() != span)
        keyFlash.assign ((size_t) span, 0.0f);

    // Card behind the keyboard.
    const auto card = getLocalBounds().toFloat().reduced (2.0f);
    juce::DropShadow (theme.shadow, 10, { 0, 2 })
        .drawForRectangle (g, card.getSmallestIntegerContainer());
    g.setColour (theme.material);
    g.fillRoundedRectangle (card, theme.cornerRadius);
    g.setColour (theme.separator);
    g.drawRoundedRectangle (card.reduced (0.5f), theme.cornerRadius, 1.0f);

    if (numSlices == 0)
    {
        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Medium")));
        g.drawText ("Load a sample to play", getLocalBounds(),
                    juce::Justification::centred);
        return;
    }

    const juce::Colour whiteFill = theme.dark ? juce::Colour (0xffe9edf6)
                                              : juce::Colours::white;
    const juce::Colour blackFill = theme.dark ? juce::Colour (0xff141628)
                                              : juce::Colour (0xff2a2a2e);

    auto drawKey = [&] (int s)
    {
        const auto  r        = keyRect (s, span).reduced (1.0f, 0.0f);
        const bool  black    = isBlackKey (s);
        const bool  enabled  = s < numSlices;
        const float flash    = keyFlash[(size_t) s];
        const float cornerR  = black ? 3.5f : 4.5f;

        juce::Colour fill = black ? blackFill : whiteFill;
        if (! enabled)
            fill = fill.withAlpha (black ? 0.35f : 0.18f);
        fill = fill.interpolatedWith (theme.accent, flash * 0.85f);

        // Rounded at the bottom only, like a real keybed.
        juce::Path key;
        key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                                 cornerR, cornerR, false, false, true, true);

        // Accent glow while the key is lit.
        if (flash > 0.0f && theme.glow > 0.0f)
        {
            g.setColour (theme.accent.withAlpha (0.5f * flash * theme.glow));
            g.strokePath (key, juce::PathStrokeType (4.0f));
        }

        g.setColour (fill);
        g.fillPath (key);
        g.setColour (theme.separator.withAlpha (black ? 0.9f : 0.55f));
        g.strokePath (key, juce::PathStrokeType (1.0f));

        // Octave labels on Cs.
        if (! black && s % 12 == 0 && enabled)
        {
            g.setColour (juce::Colour (0xff5a5f73).withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Medium")));
            g.drawText ("C" + juce::String (3 + s / 12),
                        r.reduced (2.0f).removeFromBottom (14.0f),
                        juce::Justification::centred);
        }
    };

    // Whites first, then blacks on top.
    for (int s = 0; s < span; ++s)
        if (! isBlackKey (s))
            drawKey (s);
    for (int s = 0; s < span; ++s)
        if (isBlackKey (s))
            drawKey (s);
}

//==============================================================================
void SliceGrid::mouseDown (const juce::MouseEvent& e)
{
    const int key = keyAt (e.position);
    if (key < 0 || key >= proc.getSliceEngine().getNumSlices())
        return;

    // Same mapping as MIDI: key semitone offset == slice index.
    proc.triggerSlicePad (key);

    if (key < (int) keyFlash.size())
        keyFlash[(size_t) key] = 1.0f;
    repaint();
}

void SliceGrid::timerCallback()
{
    bool any = false;
    for (auto& f : keyFlash)
    {
        if (f > 0.0f)
        {
            f = juce::jmax (0.0f, f - 0.05f);
            any = true;
        }
    }
    if (any)
        repaint();
}
