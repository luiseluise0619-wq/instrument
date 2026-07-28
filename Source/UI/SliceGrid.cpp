#include "SliceGrid.h"
#include "ThemeManager.h"
#include "TypingKeymap.h"
#include "../PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kFeltHeight  = 5.0f;   // classic felt strip above the keys
    constexpr float kLetterBand  = 14.0f;  // bottom strip reserved for the typing letter

    /** The computer-keyboard character that plays this semitone, or an empty
        string when nothing on the QWERTY keyboard reaches it.

        The table is NOT restated here: it lives in TypingKeymap.h, and the
        mapping depends on the engine (Chop restarts the upper row at the first
        slice instead of an octave up), so this inverts the real function rather
        than a copy of it. That is the whole point - a letter printed on a key
        that does not play it is worse than no letter at all. */
    juce::String typingLetterFor (int semitone, bool chopMode)
    {
        for (int i = 0; i < slyce::keymap::numKeys; ++i)
            if (slyce::keymap::semitoneFor (i, chopMode) == semitone)
                return juce::String::charToString (slyce::keymap::keys[i]).toUpperCase();

        return {};
    }
}

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
    // Synth mode: a three-octave keyboard (every key makes sound); the
    // OCT -/+ buttons shift the whole instrument a further +-2 octaves.
    if (proc.isSynthMode())
        return 36;

    // Chop mode: whole octaves, at least one, enough to cover every slice.
    // Slices sit on WHITE keys now (do-re-mi = cut order), so one octave
    // covers 7 slices. Cap at 5 octaves (35 white keys) - beyond that the
    // keys get too small to click; slices past the cap stay reachable via
    // MIDI, and the engine wraps out-of-range keys anyway.
    const int numSlices = proc.getSliceEngine().getNumSlices();
    const int octaves   = juce::jlimit (1, 5, (numSlices + 6) / 7);
    return octaves * 12;
}

juce::Rectangle<float> SliceGrid::keysArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 8.0f)
                           .withTrimmedTop (kFeltHeight + 2.0f);
}

juce::Rectangle<float> SliceGrid::keyRect (int semitone, int span) const
{
    const auto area = keysArea();

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
    const float blackW = whiteW * 0.60f;
    const float x = area.getX() + whitesBefore * whiteW - blackW * 0.5f;
    return { x, area.getY(), blackW, area.getHeight() * 0.615f };
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

    // --- Card behind the keybed -------------------------------------------
    const auto card = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (theme.shadow.withAlpha (0.16f));
    g.fillRoundedRectangle (card.translated (0.0f, 4.0f).expanded (1.5f),
                            theme.cornerRadius + 1.5f);
    g.setColour (theme.shadow.withAlpha (0.10f));
    g.fillRoundedRectangle (card.translated (0.0f, 2.0f), theme.cornerRadius);
    g.setColour (theme.material);
    g.fillRoundedRectangle (card, theme.cornerRadius);
    g.setColour (theme.separator);
    g.drawRoundedRectangle (card.reduced (0.5f), theme.cornerRadius, 1.0f);

    const bool synthMode = proc.isSynthMode();
    const bool chopMode  = proc.isChopMode();
    const int  selSlice  = proc.getSelectedSlice();

    if (numSlices == 0 && ! synthMode)
    {
        g.setColour (theme.textSecondary);
        g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Medium")));
        g.drawText ("Load a sample to play", getLocalBounds(),
                    juce::Justification::centred);
        return;
    }

    const auto keys = keysArea();

    // --- Strip above the keys: neon light bar on glow themes, felt otherwise
    {
        auto felt = keys.withY (keys.getY() - kFeltHeight - 2.0f)
                        .withHeight (kFeltHeight);

        if (theme.glow >= 0.9f)
        {
            // Cyan -> pink neon tube with a soft upward glow.
            juce::ColourGradient tube (theme.accent, felt.getX(), felt.getY(),
                                       theme.waveform, felt.getRight(), felt.getY(),
                                       false);
            g.setColour (theme.accent.withAlpha (0.16f));
            g.fillRoundedRectangle (felt.expanded (2.0f, 3.0f), 3.0f);
            g.setGradientFill (tube);
            g.fillRoundedRectangle (felt, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (felt.withHeight (1.0f));
        }
        else
        {
            // Neutral felt. It used to be theme.waveform, an accent-derived
            // colour spent on pure decoration; accent means "currently active"
            // (spec S1), so it now belongs to the pressed key and the selected
            // slice number only. Material darkened by two hairlines reads as
            // felt against the card in both light and dark themes.
            g.setColour (theme.materialStrong.overlaidWith (theme.separator)
                                             .overlaidWith (theme.separator));
            g.fillRoundedRectangle (felt, 2.0f);
            // Thin highlight so the felt reads as fabric, not a flat bar.
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRect (felt.withHeight (1.0f));
        }
    }

    const bool dark = theme.dark;

    // --- Key renderers -------------------------------------------------------
    auto drawWhite = [&] (int s)
    {
        auto r = keyRect (s, span).reduced (1.2f, 0.0f);
        const bool  enabled = synthMode || numSlices > 0;
        const float flash   = keyFlash[(size_t) s];
        const bool  hover   = (s == hoveredKey && enabled);
        const float pressed = flash;   // 0..1 visual press amount

        juce::Path key;
        key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                                 5.0f, 5.0f, false, false, true, true);

        // Accent under-glow while lit.
        if (flash > 0.02f && theme.glow > 0.0f)
        {
            g.setColour (theme.accent.withAlpha (0.45f * flash * theme.glow));
            g.strokePath (key, juce::PathStrokeType (5.0f));
        }

        // Ivory body: slightly shaded at the top (fallboard shadow), bright
        // toward the front edge. Pressing tilts the gradient darker. The ivory
        // is TINTED toward the theme accent - a cold blue-white keybed under a
        // gold or rose panel looked like it came from another plugin.
        const juce::Colour ivoryTop =
            (dark ? juce::Colour (0xffc9cfdd) : juce::Colour (0xffe9e9ee))
                .interpolatedWith (theme.accent, dark ? 0.05f : 0.03f);
        const juce::Colour ivoryBot =
            (dark ? juce::Colour (0xfff4f7ff) : juce::Colours::white)
                .interpolatedWith (theme.accent, dark ? 0.05f : 0.03f);

        juce::Colour top = ivoryTop.darker (0.10f * pressed);
        juce::Colour bot = ivoryBot.darker (0.14f * pressed);
        if (! enabled) { top = top.withAlpha (0.16f); bot = bot.withAlpha (0.16f); }

        juce::ColourGradient body (top, r.getX(), r.getY(),
                                   bot, r.getX(), r.getBottom(), false);
        if (flash > 0.0f)
        {
            body.multiplyOpacity (1.0f);
            top = top.interpolatedWith (theme.accent, flash * 0.75f);
            bot = bot.interpolatedWith (theme.accent.brighter (0.2f), flash * 0.75f);
            body = juce::ColourGradient (top, r.getX(), r.getY(),
                                         bot, r.getX(), r.getBottom(), false);
        }
        g.setGradientFill (body);
        g.fillPath (key);

        // Hover: a whisper of accent.
        if (hover && flash < 0.3f)
        {
            g.setColour (theme.accent.withAlpha (0.10f));
            g.fillPath (key);
        }

        // Recessed shadow where the key meets the felt.
        juce::ColourGradient recess (juce::Colours::black.withAlpha (enabled ? 0.22f : 0.08f),
                                     r.getX(), r.getY(),
                                     juce::Colours::transparentBlack,
                                     r.getX(), r.getY() + 9.0f, false);
        g.setGradientFill (recess);
        g.fillRect (r.withHeight (9.0f));

        // Side separation: soft dark line on the right edge.
        g.setColour (juce::Colours::black.withAlpha (dark ? 0.35f : 0.15f));
        g.fillRect (juce::Rectangle<float> (r.getRight() - 0.75f, r.getY(),
                                            0.75f, r.getHeight()));

        // Front-edge lip highlight.
        g.setColour (juce::Colours::white.withAlpha (enabled ? 0.35f : 0.08f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 2.0f, r.getBottom() - 2.5f,
                                            r.getWidth() - 4.0f, 1.2f));

        // In Chop mode a key IS a slice, so say which one. This is the link
        // that makes the window readable in a still screenshot: audio, slice
        // and key all name the same thing.
        if (chopMode && s < numSlices)
        {
            const bool sel = (s == selSlice);
            g.setColour (theme.accent.withAlpha (sel ? 1.0f : 0.62f));
            g.fillRect (juce::Rectangle<float> (r.getX() + 1.0f, r.getY(),
                                                r.getWidth() - 2.0f, sel ? 4.0f : 3.0f));
            g.setFont (juce::Font (juce::FontOptions (9.0f).withStyle ("Semibold")));
            g.drawText (juce::String (s + 1).paddedLeft ('0', 2),
                        r.withTrimmedTop (5.0f).withHeight (12.0f),
                        juce::Justification::centred);
        }

        // The computer-keyboard letter that plays this key (spec S4.7). Bottom
        // of the key, so it never meets the slice number sitting at the top.
        // Keys past the end of the QWERTY map get nothing rather than a letter
        // that lies.
        if (enabled)
        {
            const auto letter = typingLetterFor (s, chopMode);

            if (letter.isNotEmpty())
            {
                // Legible on ivory in EVERY theme: textSecondary is light in a
                // dark theme and would vanish here, so its luminosity is nudged
                // against the key it sits on rather than an invented alpha.
                // Derived from the KEY, not from the theme: these keys are
                // ivory and graphite in every theme, so theme.textSecondary
                // (a tinted lavender) is invisible on one and garish on the
                // other. The two-argument contrasting() also drags the hue -
                // it turned these letters green. The single-argument form just
                // moves lightness, which is what a pencil mark on a key does.
                g.setColour (ivoryBot.contrasting (0.72f));
                g.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Semibold")));
                g.drawText (letter, r.reduced (1.0f).removeFromBottom (kLetterBand),
                            juce::Justification::centred);
            }
        }

        // Octave labels on the Cs, stacked just above the letter.
        if (s % 12 == 0 && enabled)
        {
            g.setColour (juce::Colour (0xff3b415a)
                             .interpolatedWith (theme.accent, 0.35f)
                             .withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Semibold")));
            g.drawText ("C" + juce::String (3 + s / 12),
                        r.reduced (2.0f).withTrimmedBottom (kLetterBand)
                                        .removeFromBottom (14.0f),
                        juce::Justification::centred);
        }
    };

    auto drawBlack = [&] (int s)
    {
        auto r = keyRect (s, span);
        const bool  enabled = synthMode || numSlices > 0;
        const float flash   = keyFlash[(size_t) s];
        if (chopMode && s < numSlices)
        {
            const bool sel = (s == selSlice);
            g.setColour (theme.accent.withAlpha (sel ? 1.0f : 0.55f));
            g.fillRect (juce::Rectangle<float> (r.getX() + 1.0f, r.getY(),
                                                r.getWidth() - 2.0f, sel ? 4.0f : 3.0f));
        }
        const bool  hover   = (s == hoveredKey && enabled);

        // Drop shadow cast onto the white keys.
        g.setColour (juce::Colours::black.withAlpha (enabled ? 0.35f : 0.15f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f).expanded (1.2f, 0.0f), 4.5f);

        juce::Path key;
        key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                                 4.0f, 4.0f, false, false, true, true);

        // Accent glow while lit.
        if (flash > 0.02f && theme.glow > 0.0f)
        {
            g.setColour (theme.accent.withAlpha (0.55f * flash * theme.glow));
            g.strokePath (key, juce::PathStrokeType (5.0f));
        }

        // Glossy lacquer body, tinted toward the theme's own ground so the
        // sharps read as part of the same instrument.
        // Neutral graphite lacquer on every theme - Apple would not tint the
        // sharps to match the accent.
        juce::Colour top = juce::Colour (0xff3a3a3c);
        juce::Colour bot = juce::Colour (0xff141416);
        if (flash > 0.0f)
        {
            top = top.interpolatedWith (theme.accent, flash * 0.9f);
            bot = bot.interpolatedWith (theme.accent.darker (0.2f), flash * 0.9f);
        }
        if (! enabled) { top = top.withAlpha (0.35f); bot = bot.withAlpha (0.35f); }

        juce::ColourGradient body (top, r.getX(), r.getY(),
                                   bot, r.getX(), r.getBottom(), false);
        g.setGradientFill (body);
        g.fillPath (key);

        if (hover && flash < 0.3f)
        {
            g.setColour (theme.accent.withAlpha (0.16f));
            g.fillPath (key);
        }

        // Glossy highlight down the centre-top of the lacquer.
        {
            auto gloss = r.reduced (r.getWidth() * 0.22f, 0.0f)
                          .withTrimmedTop (3.0f)
                          .withHeight (r.getHeight() * 0.45f);
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (enabled ? 0.16f : 0.05f),
                                        gloss.getX(), gloss.getY(),
                                        juce::Colours::transparentWhite,
                                        gloss.getX(), gloss.getBottom(), false);
            g.setGradientFill (sheen);
            g.fillRoundedRectangle (gloss, 2.5f);
        }

        // Front face: the lighter lip at the bottom of a real black key.
        {
            auto lip = r.withTrimmedTop (r.getHeight() - 7.0f).reduced (1.0f, 0.0f);
            g.setColour (juce::Colour (0xff353a5c).withAlpha (enabled ? 1.0f : 0.35f)
                             .interpolatedWith (theme.accent, flash * 0.6f));
            g.fillRoundedRectangle (lip, 3.0f);
        }

        // Sharps carry a typing letter too (S D G H J on the lower row); it
        // goes just above the front lip, matching the whites' bottom placement.
        if (enabled)
        {
            const auto letter = typingLetterFor (s, chopMode);

            if (letter.isNotEmpty())
            {
                g.setColour (bot.withAlpha (1.0f).contrasting (0.62f));
                g.setFont (juce::Font (juce::FontOptions (9.0f).withStyle ("Semibold")));
                g.drawText (letter, r.withTrimmedBottom (8.0f).removeFromBottom (12.0f),
                            juce::Justification::centred);
            }
        }
    };

    // Whites first, then blacks on top.
    for (int s = 0; s < span; ++s)
        if (! isBlackKey (s))
            drawWhite (s);
    for (int s = 0; s < span; ++s)
        if (isBlackKey (s))
            drawBlack (s);

    // --- Water-drop splashes on top of the keybed ---------------------------
    for (const auto& d : drops)
    {
        const float a = juce::jlimit (0.0f, 1.0f, d.life);

        if (d.ring)
        {
            // Expanding ripple: soft outer glow + crisp ring.
            g.setColour (theme.accent.withAlpha (0.18f * a));
            g.drawEllipse (d.x - d.size, d.y - d.size * 0.55f,
                           d.size * 2.0f, d.size * 1.1f, 4.0f);
            g.setColour (theme.accent.withAlpha (0.65f * a));
            g.drawEllipse (d.x - d.size, d.y - d.size * 0.55f,
                           d.size * 2.0f, d.size * 1.1f, 1.4f);
        }
        else
        {
            // Droplet: glow halo + bright core.
            g.setColour (theme.accent.withAlpha (0.25f * a));
            g.fillEllipse (d.x - d.size, d.y - d.size,
                           d.size * 2.0f, d.size * 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.80f * a));
            g.fillEllipse (d.x - d.size * 0.45f, d.y - d.size * 0.45f,
                           d.size * 0.9f, d.size * 0.9f);
        }
    }
}

//==============================================================================
void SliceGrid::pressKey (int key, juce::Point<float> position)
{
    if (key < 0)
        return;
    // Keys past the last slice still play — the engine wraps them onto the
    // available slices, so the top of the keybed is never dead.
    if (! proc.isSynthMode() && proc.getSliceEngine().getNumSlices() <= 0)
        return;

    // Velocity from the strike position, like a real keybed: clicking near
    // the front edge of the key plays louder than up by the felt.
    const auto  r = keyRect (key, keySpan());
    const float posInKey = juce::jlimit (0.0f, 1.0f,
                                         (position.y - r.getY()) / juce::jmax (1.0f, r.getHeight()));
    const float velocity = 0.35f + 0.65f * posInKey;

    // Same mapping as MIDI: key semitone offset == slice index. The key is
    // gated — it sounds until the mouse button is released, like a real key.
    // Only one mouse-held key is tracked, so a second press (multi-touch)
    // must let go of the first or its note would never receive a release.
    if (pressedKey >= 0 && pressedKey != key)
        proc.releaseSlicePad (pressedKey);

    proc.pressSlicePad (key, velocity);
    pressedKey = key;
    // Pressing a key selects its slice, which is what lights the matching
    // lane in the waveform above.
    if (proc.isChopMode() && key < proc.getSliceEngine().getNumSlices())
        proc.setSelectedSlice (key);

    if (key < (int) keyFlash.size())
        keyFlash[(size_t) key] = 0.55f + 0.45f * velocity;   // light follows strength

    spawnSplash (key, position);   // water-drop splash where the key was struck
    repaint();
}

void SliceGrid::mouseDown (const juce::MouseEvent& e)
{
    pressKey (keyAt (e.position), e.position);
}

void SliceGrid::mouseDrag (const juce::MouseEvent& e)
{
    // Glissando: sliding across the keybed releases the old key and plays
    // the one under the pointer.
    const int key = keyAt (e.position);
    if (key == pressedKey)
        return;

    if (pressedKey >= 0)
        proc.releaseSlicePad (pressedKey);
    pressedKey = -1;

    pressKey (key, e.position);
}

void SliceGrid::mouseUp (const juce::MouseEvent&)
{
    if (pressedKey >= 0)
    {
        proc.releaseSlicePad (pressedKey);
        pressedKey = -1;
    }
}

void SliceGrid::flashKey (int semitone, float strength)
{
    if (juce::isPositiveAndBelow (semitone, (int) keyFlash.size()))
    {
        keyFlash[(size_t) semitone] = juce::jlimit (0.0f, 1.0f, strength);

        const auto r = keyRect (semitone, keySpan());
        spawnSplash (semitone, { r.getCentreX(), r.getY() + r.getHeight() * 0.30f });
        repaint();
    }
}

void SliceGrid::spawnSplash (int /*semitone*/, juce::Point<float> at)
{
    auto rnd = [this]
    {
        splashSeed = splashSeed * 1664525u + 1013904223u;
        return (float) ((splashSeed >> 8) & 0xffff) / 65535.0f;
    };

    // One expanding ripple ring...
    drops.push_back ({ at.x, at.y, 0.0f, 0.0f, 1.0f, 5.0f, true });

    // ...and a burst of droplets that arc up and fall under gravity.
    for (int i = 0; i < 6; ++i)
        drops.push_back ({ at.x, at.y,
                           (rnd() - 0.5f) * 4.0f,
                           -(2.0f + 3.0f * rnd()),
                           1.0f,
                           1.6f + 2.2f * rnd(),
                           false });

    // Hard cap so mashing the keyboard can't grow the list unbounded.
    if (drops.size() > 140)
        drops.erase (drops.begin(), drops.begin() + (long) (drops.size() - 140));
}

void SliceGrid::mouseMove (const juce::MouseEvent& e)
{
    const int key = keyAt (e.position);
    if (key != hoveredKey)
    {
        hoveredKey = key;
        repaint();
    }
}

void SliceGrid::mouseExit (const juce::MouseEvent&)
{
    if (hoveredKey != -1)
    {
        hoveredKey = -1;
        repaint();
    }
}

void SliceGrid::timerCallback()
{
    bool any = false;
    for (size_t i = 0; i < keyFlash.size(); ++i)
    {
        // A held key stays lit; released keys fade out.
        if ((int) i == pressedKey)
            continue;

        if (keyFlash[i] > 0.0f)
        {
            keyFlash[i] = juce::jmax (0.0f, keyFlash[i] - 0.05f);
            any = true;
        }
    }

    // Advance the water-splash particles.
    if (! drops.empty())
    {
        for (auto& d : drops)
        {
            if (d.ring)
            {
                d.size += 2.2f;           // ripple expands
                d.life -= 0.055f;
            }
            else
            {
                d.x += d.vx;
                d.y += d.vy;
                d.vy += 0.38f;            // gravity pulls the droplet back down
                d.life -= 0.035f;
            }
        }
        drops.erase (std::remove_if (drops.begin(), drops.end(),
                                     [] (const Drop& d) { return d.life <= 0.0f; }),
                     drops.end());
        any = true;
    }

    if (any)
        repaint();
}
