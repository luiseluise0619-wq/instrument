#include "PluginEditor.h"
#include "UI/ThemeManager.h"
#include "BinaryData.h"

#include <algorithm>

namespace
{
    // Consistent outer margin / inner padding for the Apple-style layout.
    constexpr int kMargin  = 22;
    constexpr int kPadding = 18;
    constexpr int kGap     = 16;

    // Toolbar / caption metrics.
    constexpr int kToolbarH   = 40;
    constexpr int kCaptionH   = 20;
    constexpr int kMeterW     = 30;

    // Cheap deterministic pseudo-random for the starfield (no <random> on
    // the paint path, same sky every frame).
    float hash01 (int n)
    {
        unsigned int u = (unsigned int) n;
        u = (u << 13) ^ u;
        u = u * (u * u * 15731u + 789221u) + 1376312589u;
        return (float) (u & 0x7fffffffu) / (float) 0x7fffffff;
    }

    // Neon megacity skyline rising from the horizon: dark towers, lit window
    // grids, rooftop antennas with warning lights, the occasional neon edge.
    void drawCitySkyline (juce::Graphics& g, float w, float horizonY,
                          int seed, float maxH, float alpha)
    {
        const juce::Colour cyan    (0xff00f5ff);
        const juce::Colour magenta (0xffff2daa);
        const juce::Colour purple  (0xffb026ff);

        float x = -10.0f;
        int   b = 0;

        while (x < w + 10.0f)
        {
            const float bw = 26.0f + 46.0f * hash01 (seed + b * 13 + 1);
            const float bh = maxH * (0.25f + 0.75f * hash01 (seed + b * 13 + 2));
            const float top = horizonY - bh;

            // Tower body.
            g.setColour (juce::Colour (0xff0a0c24).withAlpha (alpha));
            g.fillRect (x, top, bw, bh + 2.0f);

            // Occasional neon roof edge (cyan or pink).
            if (hash01 (seed + b * 13 + 3) > 0.55f)
            {
                const auto edge = hash01 (seed + b * 13 + 4) > 0.5f ? cyan : magenta;
                g.setColour (edge.withAlpha (0.10f * alpha));
                g.fillRect (x, top - 2.5f, bw, 5.0f);
                g.setColour (edge.withAlpha (0.75f * alpha));
                g.fillRect (x, top - 0.8f, bw, 1.6f);
            }

            // Lit windows: a sparse grid of tiny dots.
            const int cols = juce::jmax (2, (int) (bw / 8.0f));
            const int rows = juce::jmax (3, (int) (bh / 10.0f));
            for (int cy = 0; cy < rows; ++cy)
                for (int cx = 0; cx < cols; ++cx)
                {
                    const float lit = hash01 (seed + b * 977 + cy * 31 + cx * 7);
                    if (lit < 0.72f)
                        continue;

                    const auto wc = lit > 0.93f ? magenta
                                  : lit > 0.85f ? cyan
                                                : juce::Colour (0xffcfe8ff);
                    g.setColour (wc.withAlpha ((0.25f + 0.45f * hash01 (seed + b + cx + cy * 5))
                                               * alpha));
                    g.fillRect (x + 3.0f + (float) cx * (bw - 6.0f) / (float) cols,
                                top + 4.0f + (float) cy * (bh - 8.0f) / (float) rows,
                                2.0f, 2.6f);
                }

            // Antenna spire with a warning light on the tallest towers.
            if (bh > maxH * 0.7f)
            {
                const float ax = x + bw * 0.5f;
                const float ah = 10.0f + 14.0f * hash01 (seed + b * 13 + 5);
                g.setColour (juce::Colour (0xff20264a).withAlpha (alpha));
                g.fillRect (ax - 0.7f, top - ah, 1.4f, ah);
                g.setColour (magenta.withAlpha (0.30f * alpha));
                g.fillEllipse (ax - 3.2f, top - ah - 3.2f, 6.4f, 6.4f);
                g.setColour (magenta.withAlpha (0.95f * alpha));
                g.fillEllipse (ax - 1.2f, top - ah - 1.2f, 2.4f, 2.4f);
            }

            // Vertical neon sign strip down the tower face (stacked glyphs).
            if (hash01 (seed + b * 13 + 6) > 0.62f && bh > maxH * 0.4f)
            {
                const auto sc = hash01 (seed + b * 13 + 7) > 0.5f ? magenta : cyan;
                const float sx = x + bw * (0.18f + 0.5f * hash01 (seed + b * 13 + 8));
                const int   glyphs = 3 + (int) (3.9f * hash01 (seed + b * 13 + 9));

                g.setColour (sc.withAlpha (0.12f * alpha));
                g.fillRect (sx - 2.5f, top + 6.0f, 8.0f, (float) glyphs * 8.0f + 4.0f);
                for (int k = 0; k < glyphs; ++k)
                {
                    g.setColour (sc.withAlpha ((0.55f + 0.40f * hash01 (seed + b + k * 3))
                                               * alpha));
                    g.fillRect (sx, top + 9.0f + (float) k * 8.0f, 3.0f, 4.5f);
                }
            }

            // Holographic billboard floating beside a few mega-towers.
            if (hash01 (seed + b * 13 + 10) > 0.86f && bh > maxH * 0.55f)
            {
                const float hw = bw * 0.9f, hh = 14.0f;
                const float hx = x + bw * 0.2f, hy = top - hh - 14.0f;
                const auto hc = hash01 (seed + b * 13 + 11) > 0.5f ? cyan : purple;

                g.setColour (hc.withAlpha (0.08f * alpha));
                g.fillRect (hx - 3.0f, hy - 3.0f, hw + 6.0f, hh + 6.0f);
                g.setColour (hc.withAlpha (0.16f * alpha));
                g.fillRect (hx, hy, hw, hh);
                g.setColour (hc.withAlpha (0.60f * alpha));
                g.drawRect (hx, hy, hw, hh, 1.0f);
                for (int k = 1; k <= 2; ++k)   // holo scan stripes
                {
                    g.setColour (hc.withAlpha (0.35f * alpha));
                    g.fillRect (hx + 2.0f, hy + hh * (float) k / 3.0f, hw - 4.0f, 1.0f);
                }
            }

            x += bw + 2.0f;
            ++b;
        }
    }

    // A hover-car light streak: bright head dot with a fading tail.
    void drawLightStreak (juce::Graphics& g, juce::Point<float> head,
                          float length, float angleRadians, juce::Colour c)
    {
        const juce::Point<float> tail (
            head.x - length * std::cos (angleRadians),
            head.y - length * std::sin (angleRadians));

        juce::ColourGradient grad (c.withAlpha (0.85f), head.x, head.y,
                                   c.withAlpha (0.0f),  tail.x, tail.y, false);
        g.setGradientFill (grad);
        g.drawLine ({ head, tail }, 2.2f);

        g.setColour (c.withAlpha (0.25f));
        g.fillEllipse (head.x - 4.0f, head.y - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillEllipse (head.x - 1.4f, head.y - 1.4f, 2.8f, 2.8f);
    }

    // The full-bleed "Ocean Pluck" scene for the default glow theme: night
    // sky, stars, neon mountains, a sun ring, the perspective sea grid and
    // a neon megacity with hover-car streaks. Painted once into a cached image.
    void paintOceanScene (juce::Graphics& g, int wi, int hi)
    {
        const float w = (float) wi;
        const float h = (float) hi;
        const float horizonY = h * 0.42f;
        const float cx = w * 0.5f;

        const juce::Colour cyan    (0xff00f5ff);
        const juce::Colour purple  (0xffb026ff);
        const juce::Colour magenta (0xffff2daa);

        // --- Sky ------------------------------------------------------------
        {
            juce::ColourGradient sky (juce::Colour (0xff050814), 0.0f, 0.0f,
                                      juce::Colour (0xff1a0b3c), 0.0f, horizonY, false);
            sky.addColour (0.55, juce::Colour (0xff0a0d2c));
            g.setGradientFill (sky);
            g.fillRect (0.0f, 0.0f, w, horizonY + 1.0f);
        }

        // --- Stars ----------------------------------------------------------
        for (int i = 0; i < 150; ++i)
        {
            const float sx = hash01 (i * 3 + 1) * w;
            const float sy = hash01 (i * 3 + 2) * horizonY * 0.92f;
            const float sr = 0.5f + hash01 (i * 3 + 3) * 1.1f;
            const float a  = 0.10f + hash01 (i * 7 + 5) * 0.55f;

            g.setColour ((i % 9 == 0 ? cyan : juce::Colours::white).withAlpha (a));
            g.fillEllipse (sx, sy, sr, sr);
        }

        // --- Holographic HUD ring (top right): segmented arcs + tick marks ----
        {
            const float r  = juce::jmin (w, h) * 0.16f;
            const float ox = w * 0.82f, oy = h * 0.14f;

            g.setColour (purple.withAlpha (0.07f));
            g.drawEllipse (ox - r, oy - r, r * 2.0f, r * 2.0f, 9.0f);

            // Outer ring in broken segments, like a rotating interface.
            for (int s = 0; s < 6; ++s)
            {
                const float a0 = (float) s * juce::MathConstants<float>::twoPi / 6.0f
                               + 0.12f;
                const float a1 = a0 + juce::MathConstants<float>::twoPi / 6.0f - 0.24f;
                juce::Path seg;
                seg.addCentredArc (ox, oy, r, r, 0.0f, a0, a1, true);
                g.setColour ((s % 2 == 0 ? cyan : purple).withAlpha (0.40f));
                g.strokePath (seg, juce::PathStrokeType (1.6f));
            }

            // Tick marks around an inner ring.
            g.setColour (cyan.withAlpha (0.30f));
            for (int t = 0; t < 24; ++t)
            {
                const float a  = (float) t * juce::MathConstants<float>::twoPi / 24.0f;
                const float r0 = r * 0.80f, r1 = (t % 6 == 0 ? r * 0.70f : r * 0.75f);
                g.drawLine (ox + r0 * std::cos (a), oy + r0 * std::sin (a),
                            ox + r1 * std::cos (a), oy + r1 * std::sin (a), 1.0f);
            }

            g.setColour (magenta.withAlpha (0.35f));
            g.drawEllipse (ox - r * 0.52f, oy - r * 0.52f, r * 1.04f, r * 1.04f, 1.0f);
            g.setColour (cyan.withAlpha (0.55f));
            g.drawLine (ox - r * 0.10f, oy, ox + r * 0.10f, oy, 1.0f);
            g.drawLine (ox, oy - r * 0.10f, ox, oy + r * 0.10f, 1.0f);
        }

        // --- Horizon glow ----------------------------------------------------
        {
            juce::ColourGradient glow (magenta.withAlpha (0.32f), cx, horizonY,
                                       juce::Colours::transparentBlack, cx,
                                       horizonY - h * 0.16f, false);
            g.setGradientFill (glow);
            g.fillRect (0.0f, horizonY - h * 0.16f, w, h * 0.16f);
        }

        // --- Mountains (two layers, jagged, neon ridge) -----------------------
        auto ridge = [&] (int seed, float base, float amp, juce::Colour fill,
                          juce::Colour stroke, float strokeAlpha)
        {
            juce::Path m;
            m.startNewSubPath (0.0f, base);
            const int peaks = 9;
            for (int i = 0; i <= peaks; ++i)
            {
                const float px = w * (float) i / (float) peaks;
                const float py = base - amp * (0.25f + 0.75f * hash01 (seed + i * 17));
                m.lineTo (px, py);
            }
            m.lineTo (w, base);
            m.closeSubPath();

            g.setColour (fill);
            g.fillPath (m);
            g.setColour (stroke.withAlpha (strokeAlpha * 0.25f));
            g.strokePath (m, juce::PathStrokeType (4.0f));
            g.setColour (stroke.withAlpha (strokeAlpha));
            g.strokePath (m, juce::PathStrokeType (1.2f));
        };

        ridge (91, horizonY + 1.0f, h * 0.10f, juce::Colour (0xff0a0c26),
               purple, 0.35f);

        // Searchlight beams sweeping up from the city into the sky.
        {
            auto beam = [&] (float baseX, float tipX, float tipW, juce::Colour c)
            {
                juce::Path p;
                p.startNewSubPath (baseX - 3.0f, horizonY);
                p.lineTo (tipX - tipW, horizonY - h * 0.30f);
                p.lineTo (tipX + tipW, horizonY - h * 0.30f);
                p.lineTo (baseX + 3.0f, horizonY);
                p.closeSubPath();

                juce::ColourGradient grad (c.withAlpha (0.10f), baseX, horizonY,
                                           c.withAlpha (0.0f), tipX,
                                           horizonY - h * 0.30f, false);
                g.setGradientFill (grad);
                g.fillPath (p);
            };
            beam (w * 0.22f, w * 0.16f, w * 0.035f, cyan);
            beam (w * 0.68f, w * 0.76f, w * 0.045f, magenta);
        }

        // Megacity: three depth layers of towers (far -> near).
        drawCitySkyline (g, w, horizonY + 1.0f, 577, h * 0.18f, 0.35f);
        drawCitySkyline (g, w, horizonY + 1.0f, 401, h * 0.14f, 0.60f);
        drawCitySkyline (g, w, horizonY + 1.0f, 733, h * 0.09f, 1.0f);

        // --- Sea: base + perspective grid -------------------------------------
        {
            juce::ColourGradient sea (juce::Colour (0xff12082e), 0.0f, horizonY,
                                      juce::Colour (0xff050814), 0.0f, h, false);
            g.setGradientFill (sea);
            g.fillRect (0.0f, horizonY, w, h - horizonY);
        }

        // Horizon line, hot.
        g.setColour (magenta.withAlpha (0.18f));
        g.fillRect (0.0f, horizonY - 2.5f, w, 5.0f);
        g.setColour (magenta.withAlpha (0.75f));
        g.fillRect (0.0f, horizonY - 0.75f, w, 1.5f);

        // Verticals converging on the vanishing point.
        for (int k = -14; k <= 14; ++k)
        {
            const float xTop = cx + (float) k * w * 0.012f;
            const float xBot = cx + (float) k * w * 0.085f;
            g.setColour (magenta.withAlpha (k == 0 ? 0.10f : 0.13f));
            g.drawLine (xTop, horizonY, xBot, h, 1.0f);
        }

        // Horizontals rushing toward the viewer.
        for (int row = 1; row <= 9; ++row)
        {
            const float t = (float) row / 9.0f;
            const float y = horizonY + (h - horizonY) * t * t * 1.04f;
            if (y > h) break;
            g.setColour (magenta.interpolatedWith (cyan, 0.25f)
                             .withAlpha (0.06f + 0.14f * t));
            g.drawLine (0.0f, y, w, y, t > 0.6f ? 1.4f : 1.0f);
        }

        // --- Hover-car light streaks crossing the sky ---------------------------
        drawLightStreak (g, { w * 0.30f, h * 0.10f }, w * 0.13f,  0.06f, cyan);
        drawLightStreak (g, { w * 0.58f, h * 0.20f }, w * 0.10f, -0.05f, magenta);
        drawLightStreak (g, { w * 0.14f, h * 0.27f }, w * 0.08f,  0.10f, purple);
        drawLightStreak (g, { w * 0.86f, h * 0.32f }, w * 0.09f, -0.12f, cyan);
        drawLightStreak (g, { w * 0.44f, h * 0.055f }, w * 0.07f,  0.03f, magenta);

        // --- Digital data-rain columns (Matrix-style falling dashes) ------------
        for (int col = 0; col < 5; ++col)
        {
            const float dx = w * (0.06f + 0.9f * hash01 (col * 11 + 500));
            const float startY = h * 0.02f + h * 0.06f * hash01 (col * 11 + 501);
            const int   n = 7 + (int) (6.9f * hash01 (col * 11 + 502));

            for (int k = 0; k < n; ++k)
            {
                const float dy = startY + (float) k * 7.0f;
                if (dy > horizonY - h * 0.14f) break;
                const float fade = 1.0f - (float) k / (float) n;
                g.setColour (cyan.withAlpha ((0.05f + 0.22f * fade)
                                             * (0.5f + 0.5f * hash01 (col * 97 + k))));
                g.fillRect (dx, dy, 1.6f, 4.0f);
            }
        }

        // --- Cyber rain: sparse diagonal streaks over the whole scene -----------
        for (int i = 0; i < 46; ++i)
        {
            const float rx = w * hash01 (i * 13 + 700);
            const float ry = h * hash01 (i * 13 + 701);
            const float rl = 8.0f + 14.0f * hash01 (i * 13 + 702);
            g.setColour (juce::Colour (0xffaee7ff)
                             .withAlpha (0.03f + 0.05f * hash01 (i * 13 + 703)));
            g.drawLine (rx, ry, rx - rl * 0.18f, ry + rl, 1.0f);
        }

        // --- CRT scanlines + glitch slices (subtle, over the whole scene) -----
        g.setColour (juce::Colours::black.withAlpha (0.065f));
        for (float sy = 0.0f; sy < h; sy += 3.0f)
            g.fillRect (0.0f, sy, w, 1.0f);

        for (int i = 0; i < 5; ++i)
        {
            const float gy = h * (0.08f + 0.82f * hash01 (i * 7 + 300));
            const float gw = w * (0.10f + 0.24f * hash01 (i * 7 + 301));
            const float gx = w * hash01 (i * 7 + 302) - gw * 0.5f;
            g.setColour (cyan.withAlpha (0.06f));
            g.fillRect (gx + 3.0f, gy, gw, 2.0f);
            g.setColour (magenta.withAlpha (0.06f));
            g.fillRect (gx - 3.0f, gy + 2.0f, gw, 2.0f);
        }

        // --- Purple haze rising from the horizon (city light pollution) --------
        {
            juce::ColourGradient haze (purple.withAlpha (0.10f), cx, horizonY,
                                       juce::Colours::transparentBlack, cx,
                                       horizonY - h * 0.24f, false);
            g.setGradientFill (haze);
            g.fillRect (0.0f, horizonY - h * 0.24f, w, h * 0.24f);
        }

        // --- Soft blooms + corner vignette so panels stay readable -------------
        {
            juce::ColourGradient bloom (cyan.withAlpha (0.07f), w * 0.16f, 0.0f,
                                        juce::Colours::transparentBlack,
                                        w * 0.16f, h * 0.5f, true);
            g.setGradientFill (bloom);
            g.fillRect (0.0f, 0.0f, w, h);
        }
        {
            juce::ColourGradient vig (juce::Colours::transparentBlack, cx, h * 0.45f,
                                      juce::Colour (0xff050814).withAlpha (0.55f),
                                      0.0f, h, true);
            vig.addColour (0.72, juce::Colours::transparentBlack);
            g.setGradientFill (vig);
            g.fillRect (0.0f, 0.0f, w, h);
        }
    }

    // Artwork skin, Cymatics-style: the image is anchored to the TOP and
    // fitted to the window width so the hero (the bike) stays completely
    // unobstructed; only its lower part fades into the panel zone, and
    // everything below the image is solid background.
    // Backdrop for the flat themes: a plain two-stop gradient reads cheap at
    // this size, so add a soft accent aura, a vignette and a fine grain. It
    // is rendered ONCE into a cached image, so none of this costs per-frame.
    void paintStudioBackdrop (juce::Graphics& g, int wi, int hi, const Theme& theme)
    {
        const float w = (float) wi;
        const float h = (float) hi;

        // (a) Base gradient, very slightly tinted toward the accent so each
        //     colourway feels like its own room rather than one grey box.
        const auto top    = theme.bgTop.interpolatedWith (theme.accent, theme.dark ? 0.015f : 0.008f);
        const auto bottom = theme.bgBottom;
        juce::ColourGradient bg (top, 0.0f, 0.0f, bottom, 0.0f, h, false);
        bg.addColour (0.55, bottom.interpolatedWith (top, 0.35f));
        g.setGradientFill (bg);
        g.fillAll();

        // (b) Accent aura behind the header - the light source of the panel.
        {
            const float r = w * 0.85f;
            juce::ColourGradient aura (theme.accent.withAlpha (theme.dark ? 0.055f : 0.04f),
                                       w * 0.5f, -h * 0.06f,
                                       juce::Colours::transparentBlack,
                                       w * 0.5f + r, -h * 0.06f, true);
            g.setGradientFill (aura);
            g.fillRect (0.0f, 0.0f, w, h * 0.6f);
        }

        // (c) Cool counter-light low on the right keeps the lower half alive.
        {
            const float r = w * 0.55f;
            juce::ColourGradient low (theme.waveform.withAlpha (theme.dark ? 0.03f : 0.02f),
                                      w * 0.88f, h * 0.92f,
                                      juce::Colours::transparentBlack,
                                      w * 0.88f + r, h * 0.92f, true);
            g.setGradientFill (low);
            g.fillRect (w * 0.3f, h * 0.45f, w * 0.7f, h * 0.55f);
        }

        // (d) Vignette: pulls the eye to the centre and stops the corners
        //     from looking like flat paint.
        {
            const float r = juce::jmax (w, h) * 0.78f;
            juce::ColourGradient vig (juce::Colours::transparentBlack, w * 0.5f, h * 0.5f,
                                      juce::Colours::black.withAlpha (theme.dark ? 0.34f : 0.10f),
                                      w * 0.5f + r, h * 0.5f, true);
            g.setGradientFill (vig);
            g.fillAll();
        }

        // (e) Fine deterministic grain. Flat dark gradients band badly on
        //     cheap panels; a whisper of noise dithers that away.
        {
            const float a = theme.dark ? 0.020f : 0.012f;
            g.setColour (juce::Colours::white.withAlpha (a));
            for (int i = 0; i < 5200; ++i)
            {
                const float gx = hash01 (i * 2 + 1) * w;
                const float gy = hash01 (i * 2 + 7919) * h;
                g.fillRect (gx, gy, 1.0f, 1.0f);
            }
        }
    }

    // FL-style typing keys: bottom row = C3 octave, top row = C4 octave and
    // onward through i 9 o 0 p [ = ] (C5..G5), matching FL Studio's layout.
    // ',' is deliberately NOT mapped: it would duplicate Q's C4, and two keys
    // driving one note means releasing either kills the other's sound.
    const juce::String kTypingKeys ("zsxdcvgbhnjmq2w3er5t6y7ui9o0p[=]");

    int typingKeySemitone (int i)     { return i; }   // 12 keys per row, contiguous

    bool physicalKeyDown (juce::juce_wchar c)
    {
        if (juce::KeyPress::isKeyCurrentlyDown ((int) c))
            return true;
        const juce::juce_wchar up = juce::CharacterFunctions::toUpperCase (c);
        return up != c && juce::KeyPress::isKeyCurrentlyDown ((int) up);
    }
}

//==============================================================================
VocalChopAudioProcessorEditor::VocalChopAudioProcessorEditor (VocalChopAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      sensitivityKnob ("Sensitivity"),
      waveform (p),
      chordBar (p),
      sliceGrid (p),
      fxRack (p.getAPVTS())
{
    // Restyle all buttons / combos / menus with the shared Apple look.
    setLookAndFeel (&appleLaf);

    // The editor itself plays notes from the computer keyboard.
    setWantsKeyboardFocus (true);

    // --- Title: lowercase wordmark + quiet category caption ---
    titleLabel.setText ("slyce", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (26.0f).withStyle ("Bold"))
                            .withExtraKerningFactor (0.02f));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    // Laid out but never drawn: paintContent renders the wordmark with a
    // gradient + bloom that a plain Label cannot do.
    addChildComponent (titleLabel);

    // Shows what is loaded rather than a fixed tagline once there IS something
    // loaded: pressing Demo Vocal ten times looked identical without it.
    subtitleLabel.setText ("VOCAL CHOP INSTRUMENT", juce::dontSendNotification);
    subtitleLabel.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Medium"))
                               .withExtraKerningFactor (0.18f));
    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (subtitleLabel);

    // --- Preset menu (NOT an APVTS param) ---
    presetLabel.setText ("Preset", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (presetLabel);

    {
        rebuildPresetMenu();
        presetBox.setSelectedId (1, juce::dontSendNotification);
        presetBox.setJustificationType (juce::Justification::centred);
        presetBox.onChange = [this]
        {
            const int id = presetBox.getSelectedId();
            if (id <= 0)
                return;

            if (id == kSavePresetId)          // "Save preset..."
            {
                presetBox.setSelectedId (0, juce::dontSendNotification);
                promptSavePreset();
                return;
            }
            if (id >= kUserPresetBaseId)      // one of the user's own
            {
                const auto names = VocalChopAudioProcessor::getUserPresetNames();
                const int u = id - kUserPresetBaseId;
                if (u < names.size())
                    processor.loadUserPreset (names[u]);
                refreshChildren();
                const auto shownU = presetBox.getText();
                presetBox.setSelectedId (0, juce::dontSendNotification);
                presetBox.setText (shownU, juce::dontSendNotification);
                grabKeysSoon();
                return;
            }

            processor.applyPreset (id - 1);
            refreshChildren();
            // Deselect (keeping the text) so picking the SAME preset again
            // re-applies it - "reset to Init" must always work.
            const auto shown = presetBox.getText();
            presetBox.setSelectedId (0, juce::dontSendNotification);
            presetBox.setText (shown, juce::dontSendNotification);
        };
        addAndMakeVisible (presetBox);
    }

    // --- Theme selector ---
    int themeId = 1;
    for (const auto& t : ThemeManager::themes())
        themeBox.addItem (t.name, themeId++);
    themeBox.setSelectedId (ThemeManager::current() + 1, juce::dontSendNotification);
    themeBox.setJustificationType (juce::Justification::centred);
    themeBox.onChange = [this]
    {
        ThemeManager::setIndex (themeBox.getSelectedId() - 1);
        refreshChildren();
        repaint();
    };
    addAndMakeVisible (themeBox);

    // --- Load button ---
    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

    // One-click start: load the embedded demo vocal (the processor slices it,
    // falling back to a grid when transients are sparse — reflect that here).
    // --- LOOPER tab toggle: swap the mid section for the loop station ---
    looperTabButton.setClickingTogglesState (true);
    looperTabButton.onClick = [this]
    {
        showLooper = looperTabButton.getToggleState();
        looperPanel.setVisible (showLooper);
        if (showLooper)
            looperPanel.toFront (false);
        grabKeysSoon();   // keep playing while looping
        resized();
        repaint();
    };
    addAndMakeVisible (looperTabButton);

    // --- Ambient mode -------------------------------------------------------
    ambientButton.setTooltip ("Full-panel visualiser - for leaving Slyce running on a spare screen");
    ambientButton.onClick = [this] { setAmbient (true); };
    addAndMakeVisible (ambientButton);

    ambientPanel.onExit = [this] { setAmbient (false); };
    addChildComponent (ambientPanel);

    addChildComponent (looperPanel);   // hidden until the tab is opened

    // --- Licensing: UNLOCK button (demo builds only) + overlay panel ---
    unlockButton.onClick = [this]
    {
        unlockPanel.setVisible (true);
        unlockPanel.toFront (true);
    };
    addAndMakeVisible (unlockButton);
    unlockButton.setVisible (! processor.isLicensed());
    addChildComponent (unlockPanel);

    // --- First-run quick start + "?" help ---
    helpButton.onClick = [this]
    {
        welcomePanel.setVisible (true);
        welcomePanel.toFront (true);
    };
    addAndMakeVisible (helpButton);
    addChildComponent (welcomePanel);

    // Hover help everywhere a first-timer might hesitate.
    // "Demo" read as "demo version" to the first person who saw it. It loads a
    // built-in VOCAL, which is a different thing entirely, and it sits next to
    // "Load Sample" - so the pair now reads "ours" and "yours".
    demoButton.setTooltip ("Loads one of 10 built-in vocals to chop - press again for the next");
    loadButton.setTooltip ("Load your own audio (wav/mp3...) to chop across the keys");
    engineBox.setTooltip ("Chop = slices of loaded audio.  Synth = 376 built-in sounds.  "
                          "Sampled = load an SFZ bank of REAL recordings (Load button).  "
                          "Melody = play the loaded sample as pitched notes");
    synthWaveBox.setTooltip ("Basic oscillator shape for the synth");
    instrumentBox.setTooltip ("376 built-in sounds, organised by category - start with FEATURED");
    looperTabButton.setTooltip ("Loop station: record and stack up to 6 loop tracks from your keyboard");
    themeBox.setTooltip ("Color themes and artwork skins");
    presetBox.setTooltip ("Full-plugin presets (sound + FX together)");
    sliceModeBox.setTooltip ("How the audio gets cut: at transients or on a beat grid");
    gridBox.setTooltip ("Grid density when slicing by beats");
    helpButton.setTooltip ("Show the quick-start guide again");
    unlockButton.setTooltip ("Enter your license key (demo mutes 2 s every minute)");

    demoButton.setTriggeredOnMouseDown (true);   // instant response
    demoButton.onClick = [this]
    {
        if (processor.loadDemoSample())
        {
            syncSliceControls();
            refreshChildren();
            grabKeysSoon();
        }
    };
    addAndMakeVisible (demoButton);

    // --- Engine mode: sample chopping vs. built-in synth ---
    engineBox.addItem ("Chop",  1);
    engineBox.addItem ("Synth", 2);
    engineBox.addItem ("Sampled", 3);   // SFZ multisample banks (real recordings)
    engineBox.addItem ("Melody", 4);    // the loaded sample, pitched across keys
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getAPVTS(), "engine", engineBox));
    engineBox.onChange = [this] { syncEngineEnablement(); refreshChildren();
                                  refreshInstrumentHero(); grabKeysSoon(); };
    // Attached but not shown - the tabs below are its face. Keeping the combo
    // means the host automation binding and every saved session still work.
    addChildComponent (engineBox);

    // --- Engine tabs --------------------------------------------------------
    // Four engines, and half the controls change meaning between them. A combo
    // hid that: you had to open a menu to find out which mode you were in.
    for (int i = 0; i < 4; ++i)
    {
        auto& t = engineTab[i];
        t.setButtonText (engineBox.getItemText (i));
        t.setClickingTogglesState (false);
        t.setConnectedEdges (juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        t.onClick = [this, i]
        {
            engineBox.setSelectedItemIndex (i, juce::sendNotificationSync);
            refreshChildren();
        };
        t.setTooltip (engineBox.getTooltip());
        addAndMakeVisible (t);
    }

    // --- Instrument hero ----------------------------------------------------
    instCategoryLabel.setJustificationType (juce::Justification::centredLeft);
    instCategoryLabel.setInterceptsMouseClicks (false, false);
    instCategoryLabel.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Medium")));
    addAndMakeVisible (instCategoryLabel);

    instNameLabel.setJustificationType (juce::Justification::centredLeft);
    instNameLabel.setInterceptsMouseClicks (false, false);
    // The largest type on the screen, on purpose: this is the control the
    // first tester could not find at all.
    instNameLabel.setFont (juce::Font (juce::FontOptions (27.0f).withStyle ("Bold")));
    addAndMakeVisible (instNameLabel);

    instBrowseButton.setTooltip ("Browse all 376 instruments by category");
    instBrowseButton.onClick = [this]
    {
        // The combo still OWNS the 376-entry categorised menu - this shows a
        // copy of it - so there is exactly one list in the program to keep
        // correct. Going through the menu rather than ComboBox::showPopup()
        // is what lets the combo itself stay hidden.
        if (auto* root = instrumentBox.getRootMenu())
        {
            juce::PopupMenu menu (*root);
            menu.setLookAndFeel (&appleLaf);
            menu.showMenuAsync (juce::PopupMenu::Options()
                                    .withTargetComponent (&instBrowseButton)
                                    .withMinimumWidth (240)
                                    .withMaximumNumColumns (3),
                                [this] (int id)
                                {
                                    if (id != 0)
                                        instrumentBox.setSelectedId (id, juce::sendNotificationSync);
                                });
        }
    };
    addAndMakeVisible (instBrowseButton);

    // Eight category chips: one click lands on the first voice of a family,
    // instead of scrolling a 376-entry menu to find out what is in there.
    {
        static const char* chips[kNumChips] =
            { "BASS", "DRUMS", "LEAD", "PAD", "KEYS", "PLUCK", "VOCAL", "BELL" };
        for (int i = 0; i < kNumChips; ++i)
        {
            auto& c = categoryChip[i];
            const juce::String cat (chips[i]);
            c.setButtonText (cat.substring (0, 1) + cat.substring (1).toLowerCase());
            c.setClickingTogglesState (false);
            c.setTooltip ("Jump to the " + c.getButtonText() + " voices");
            c.onClick = [this, cat] { jumpToCategory (cat); };
            addAndMakeVisible (c);
        }
    }

    synthWaveBox.addItem ("Saw", 1);
    synthWaveBox.addItem ("Square", 2);
    synthWaveBox.addItem ("Sine", 3);
    synthWaveBox.addItem ("Triangle", 4);
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getAPVTS(), "synthWave", synthWaveBox));
    addAndMakeVisible (synthWaveBox);

    // Instrument menu: a flat 225-row list scrolled past the screen edge and
    // appeared to show "only a few" items depending on where the popup
    // opened. Structure it instead as a short root (Featured shelf) plus one
    // submenu per category - always the same compact menu.
    {
        static const char* featured[] = { "Drum Kit", "Vox Choir", "Vox Pluck",
                                          "Supersaw Lead", "Rage Bell", "Memphis 808",
                                          "Soul Keys", "Future Bass", "Trance Pluck",
                                          "Bass Pad", "Royal Grand", "Hyper Saw" };
        const auto names = VocalChopAudioProcessor::getInstrumentNames();
        const auto cats  = VocalChopAudioProcessor::getInstrumentCategories();

        auto* root = instrumentBox.getRootMenu();
        root->addSectionHeader ("FEATURED");
        for (auto* f : featured)
        {
            const int idx = names.indexOf (f);
            if (idx >= 0)
                instrumentBox.addItem (f, 1000 + idx);
        }
        root->addSeparator();

        // SFZ banks the user loaded themselves - remembered across sessions
        // so a violin or piano they hunted down never just disappears.
        {
            const auto rNames = VocalChopAudioProcessor::getRecentSfzNames();
            mySamplePaths     = VocalChopAudioProcessor::getRecentSfzPaths();
            if (! rNames.isEmpty())
            {
                root->addSectionHeader ("MY SAMPLES");
                for (int r = 0; r < rNames.size(); ++r)
                    instrumentBox.addItem (rNames[r], 5000 + r);
                root->addSeparator();
            }
        }

        // One submenu per contiguous category block (ids stay index + 1).
        int i = 0;
        while (i < names.size())
        {
            const juce::String cat = cats[i];
            juce::PopupMenu sub;
            while (i < names.size() && cats[i] == cat)
            {
                sub.addItem (i + 1, names[i]);
                ++i;
            }
            root->addSubMenu (cat, sub);
        }
    }

    instrumentBox.setTextWhenNothingSelected ("Instrument");
    if (processor.getCurrentInstrument() > 0)
        instrumentBox.setSelectedId (processor.getCurrentInstrument() + 1,
                                     juce::dontSendNotification);
    instrumentBox.onChange = [this]
    {
        const int id = instrumentBox.getSelectedId();
        if (id >= 5000)
        {
            // MY SAMPLES: reload a remembered SFZ bank (switches to Sampled).
            // Ids map to paths captured at menu-build time (mySamplePaths) -
            // duplicate display names stay distinct and reorders can't drift.
            const int r = id - 5000;
            juce::String err;
            if (r >= 0 && r < mySamplePaths.size()
                  && processor.loadSfzBank (juce::File (mySamplePaths[r]), err))
            {
                refreshChildren();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Slyce",
                    "Could not reload this sample bank - its files may have "
                    "moved or been deleted.\n" + err);
            }
            grabKeysSoon();
            return;
        }
        if (id >= 1000)
            processor.applyInstrument (id - 1000);   // featured shelf
        else if (id > 0)
            processor.applyInstrument (id - 1);
        // NO refreshChildren() here: an instrument change touches neither the
        // waveform, the slice grid nor the layout - the knob attachments
        // update themselves. The full refresh made every pick stutter.
        grabKeysSoon();   // pick a patch, play it immediately
    };
    // Hidden: the hero shows the name, Browse opens the menu, and the
    // steppers move through it. The combo remains as the one list.
    addChildComponent (instrumentBox);

    // Auditioning 376 sounds through a nested menu means six clicks per
    // sound. These step one at a time, so you can hold the keyboard down and
    // walk the whole list with the other hand.
    instPrevButton.setTooltip ("Previous instrument");
    instNextButton.setTooltip ("Next instrument");
    instPrevButton.setTriggeredOnMouseDown (true);
    instNextButton.setTriggeredOnMouseDown (true);
    instPrevButton.setRepeatSpeed (420, 90);   // hold to scan
    instNextButton.setRepeatSpeed (420, 90);
    instPrevButton.onClick = [this] { stepInstrument (-1); };
    instNextButton.onClick = [this] { stepInstrument ( 1); };
    addAndMakeVisible (instPrevButton);
    addAndMakeVisible (instNextButton);

    addAndMakeVisible (chordBar);

    // --- Slice mode (initialised from the engine so restored state shows) ---
    auto& engine = processor.getSliceEngine();

    sliceModeBox.addItem ("Transient", 1);
    sliceModeBox.addItem ("Grid", 2);
    sliceModeBox.setSelectedId (engine.getMode() == SliceEngine::Grid ? 2 : 1,
                                juce::dontSendNotification);
    sliceModeBox.setJustificationType (juce::Justification::centred);
    sliceModeBox.onChange = [this] { applySlicing(); syncEngineEnablement(); };
    addAndMakeVisible (sliceModeBox);

    for (int div : { 4, 8, 16, 32 })
        gridBox.addItem (juce::String (div) + " slices", div);
    const int restoredDiv = engine.getGridDivision();
    gridBox.setSelectedId ((restoredDiv == 4 || restoredDiv == 8
                            || restoredDiv == 16 || restoredDiv == 32) ? restoredDiv : 16,
                           juce::dontSendNotification);
    gridBox.setJustificationType (juce::Justification::centred);
    gridBox.onChange = [this] { applySlicing(); };
    addAndMakeVisible (gridBox);

    sensitivityKnob.getSlider().setRange (0.05, 0.9, 0.01);
    sensitivityKnob.getSlider().setValue (engine.getSensitivity(), juce::dontSendNotification);
    sensitivityKnob.getSlider().onValueChange = [this] { applySlicing(); };
    addAndMakeVisible (sensitivityKnob);

    // --- Octave shift: +-2 octaves on top of the 3-octave keyboard ---
    if (auto* octParam = processor.getAPVTS().getParameter ("synthOctave"))
    {
        octAttachment = std::make_unique<juce::ParameterAttachment> (
            *octParam,
            [this] (float v)
            {
                octLabel.setText ("Oct " + juce::String ((int) std::round (v)),
                                  juce::dontSendNotification);
            });
        octAttachment->sendInitialUpdate();

        auto shiftOctave = [this, octParam] (int delta)
        {
            const float cur = octParam->convertFrom0to1 (octParam->getValue());
            const float next = (float) juce::jlimit (-2, 2, (int) std::round (cur) + delta);
            octParam->setValueNotifyingHost (octParam->convertTo0to1 (next));
            grabKeysSoon();
        };
        octDownButton.onClick = [shiftOctave] { shiftOctave (-1); };
        octUpButton.onClick   = [shiftOctave] { shiftOctave (+1); };
        // Fire on press and auto-repeat while held: octave surfing feels
        // instantaneous instead of click-release-click.
        for (auto* b : { &octDownButton, &octUpButton })
        {
            b->setTriggeredOnMouseDown (true);
            b->setRepeatSpeed (350, 110);
        }
    }
    octLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (octDownButton);
    addAndMakeVisible (octLabel);
    addAndMakeVisible (octUpButton);

    // --- Envelope knobs ---
    addKnob (attackKnob,  "attack",  "Attack");
    addKnob (decayKnob,   "decay",   "Decay");
    addKnob (sustainKnob, "sustain", "Sustain");
    addKnob (releaseKnob, "release", "Release");

    // --- Pitch / Tone knobs ---
    addKnob (pitchKnob,   "pitch",     "Pitch");
    addKnob (formantKnob, "formant",   "Formant");
    addKnob (mixKnob,     "mix",       "Mix");
    addKnob (widthKnob,   "width",     "Width");
    addKnob (grainKnob,   "grainSize",   "Grain");
    addKnob (detuneKnob,  "synthDetune", "Detune");

    // --- Synth module knobs ---
    addKnob (unisonKnob,  "synthUnison",  "Unison");
    addKnob (spreadKnob,  "synthSpread",  "Spread");
    addKnob (subKnob,     "synthSub",     "Sub");
    addKnob (noiseKnob,   "synthNoise",   "Noise");
    addKnob (fmKnob,      "synthFM",      "FM");
    addKnob (vibratoKnob, "synthVibrato", "Vibrato");
    addKnob (chorusKnob,  "synthChorus",  "Chorus");
    addKnob (lfoRateKnob, "synthLfoRate", "LFO Rate");
    addKnob (motionKnob,  "synthLfoAmt",  "Motion");
    addKnob (glideKnob,   "synthGlide",   "Glide");

    // --- Performance macros: the three most prominent knobs on screen ---
    addKnob (hypeKnob,  "macroHype",  "HYPE");
    addKnob (spaceKnob, "macroSpace", "SPACE");
    addKnob (dirtKnob,  "macroDirt",  "DIRT");
    // A macro moves many parameters at once, and the whole point of it is that
    // you do not have to know which. Naming them on the panel is what turns
    // the dial from a mystery into a shortcut.
    hypeKnob ->setSubCaption ("unison - drive - air");
    spaceKnob->setSubCaption ("reverb - width - delay");
    dirtKnob ->setSubCaption ("saturation - noise - grain");

    // Hovering a macro lights every dial it moves. The sub-caption names them
    // in words; this points at them, which is the difference between reading
    // that HYPE touches unison and seeing WHICH unison dial that is.
    auto learn = [this] (std::vector<KnobComponent*> driven)
    {
        return [this, driven] (bool on)
        {
            for (auto* k : driven)
                if (k != nullptr) k->setLearnGlow (on);
        };
    };
    hypeKnob ->onHoverChanged = learn ({ unisonKnob.get(), spreadKnob.get(),
                                         fxRack.driveKnob(), outputGainKnob.get() });
    spaceKnob->onHoverChanged = learn ({ fxRack.reverbKnob(), fxRack.delayKnob(),
                                         widthKnob.get(), grainMixKnob.get() });
    dirtKnob ->onHoverChanged = learn ({ fxRack.driveKnob(), noiseKnob.get(),
                                         grainKnob.get(), detuneKnob.get() });

    // --- Filter knobs + combo ---
    addKnob (filterCutoffKnob, "filterCutoff", "Cutoff");
    addKnob (filterResoKnob,   "filterReso",   "Reso");

    filterTypeBox.addItem ("Off",       1);
    filterTypeBox.addItem ("Low Pass",  2);
    filterTypeBox.addItem ("High Pass", 3);
    filterTypeBox.addItem ("Band Pass", 4);
    filterTypeBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (filterTypeBox);
    comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (
        processor.getAPVTS(), "filterType", filterTypeBox));

    // --- Playback: toggles, play mode combo, output gain knob ---
    addAndMakeVisible (reverseButton);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (
        processor.getAPVTS(), "reverse", reverseButton));

    addAndMakeVisible (pingpongButton);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (
        processor.getAPVTS(), "pingpong", pingpongButton));

    // --- Arp + pump ---------------------------------------------------------
    arpModeBox.setTooltip ("Arpeggiator: hold a chord and it plays the notes in "
                           "time with your DAW");
    for (const auto* m : { "Off", "Up", "Down", "Up-Down", "Random" })
        arpModeBox.addItem (m, arpModeBox.getNumItems() + 1);
    arpModeBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (arpModeBox);
    comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (
        processor.getAPVTS(), "arpMode", arpModeBox));

    arpRateBox.setTooltip ("How fast the arp steps");
    for (const auto* r : { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" })
        arpRateBox.addItem (r, arpRateBox.getNumItems() + 1);
    arpRateBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (arpRateBox);
    comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (
        processor.getAPVTS(), "arpRate", arpRateBox));

    arpOctBox.setTooltip ("How many octaves the pattern climbs");
    for (const auto* o : { "1 oct", "2 oct", "3 oct" })
        arpOctBox.addItem (o, arpOctBox.getNumItems() + 1);
    arpOctBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (arpOctBox);
    comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (
        processor.getAPVTS(), "arpOct", arpOctBox));

    pumpRateBox.setTooltip ("How often the pump ducks");
    for (const auto* r : { "1 bar", "1/2", "1/4", "1/8" })
        pumpRateBox.addItem (r, pumpRateBox.getNumItems() + 1);
    pumpRateBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (pumpRateBox);
    comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (
        processor.getAPVTS(), "pumpRate", pumpRateBox));

    addKnob (arpGateKnob, "arpGate", "Gate");
    addKnob (pumpKnob,    "pumpAmt", "Pump");
    arpGateKnob->getSlider().setTooltip ("How long each arp note holds");
    if (glideKnob != nullptr)
        glideKnob->getSlider().setTooltip ("Portamento: each note slides in from the "
                                           "one before it (0 = off)");
    pumpKnob->getSlider().setTooltip ("Sidechain pump: ducks the whole mix on the "
                                      "beat, no routing needed");

    delaySyncBox.setTooltip ("Delay time locked to your DAW's tempo "
                             "(Free = the plugin's own 350 ms)");
    delaySyncBox.addItem ("Free", 1);
    delaySyncBox.addItem ("1/4",  2);
    delaySyncBox.addItem ("1/4.", 3);
    delaySyncBox.addItem ("1/8",  4);
    delaySyncBox.addItem ("1/8.", 5);
    delaySyncBox.addItem ("1/16", 6);
    delaySyncBox.addItem ("1/32", 7);
    delaySyncBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (delaySyncBox);
    comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (
        processor.getAPVTS(), "delaySync", delaySyncBox));

    playModeBox.addItem ("Gate",     1);
    playModeBox.addItem ("One-Shot", 2);
    playModeBox.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (playModeBox);
    comboAttachments.push_back (std::make_unique<ComboBoxAttachment> (
        processor.getAPVTS(), "playMode", playModeBox));

    addKnob (outputGainKnob, "outputGain", "Output");
    addKnob (grainMixKnob,   "grainMix",   "Texture");

    // --- Views ---
    waveform.onSampleDropped = [this]
    {
        syncSliceControls();
        refreshChildren();
        grabKeysSoon();
    };
    addAndMakeVisible (waveform);
    addAndMakeVisible (meter);
    addAndMakeVisible (sliceGrid);
    addAndMakeVisible (fxRack);

    // Everything lives on a fixed 1080x1060 canvas that scales as one unit,
    // so the window can shrink to laptop size without any layout cramming.
    while (getNumChildComponents() > 0)
        content.addChildComponent (getChildComponent (0));
    content.setInterceptsMouseClicks (false, true);   // background clicks reach us
    addAndMakeVisible (content);
    addAndMakeVisible (tooltipWindow);   // tooltips live OUTSIDE the scaled canvas

    if (! WelcomePanel::hasSeenWelcome())
    {
        welcomePanel.setVisible (true);
        welcomePanel.toFront (false);
    }

    setResizable (true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setResizeLimits (kBaseW * 45 / 100, kBaseH * 45 / 100,
                     kBaseW * 160 / 100, kBaseH * 160 / 100);

    // Open at a size that FITS - and "fits" has to mean inside the HOST's
    // window, not inside the monitor. A DAW wraps the editor in its own
    // frame: FL Studio adds a title bar and a plugin toolbar, Ableton docks
    // it in a strip. Reserving 80px for that was optimistic, and on a 1080p
    // laptop the editor opened taller than the space FL had for it, so the
    // bottom of the plugin was simply unreachable.
    //
    // 260px of vertical headroom covers the DAW frame plus a taskbar, and
    // the cap comes down to 72%: a plugin that opens slightly small and can
    // be dragged bigger beats one that opens off the bottom of the screen.
    {
        float fit = 0.72f;
        if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto ua = disp->userArea;
            fit = juce::jmin (0.72f,
                              (float) (ua.getWidth()  - 120) / (float) kBaseW,
                              (float) (ua.getHeight() - 260) / (float) kBaseH);
            fit = juce::jmax (0.45f, fit);
        }
        setSize ((int) (kBaseW * fit), (int) (kBaseH * fit));
    }

    refreshChildren();
    startTimerHz (30);   // typing-key watchdog (stuck-note guard)

    processor.addChangeListener (this);
}

VocalChopAudioProcessorEditor::~VocalChopAudioProcessorEditor()
{
    processor.removeChangeListener (this);
    stopTimer();
    scanTypingKeys (true);   // closing the editor must not leave notes held
    setLookAndFeel (nullptr);
}

//==============================================================================
void VocalChopAudioProcessorEditor::addKnob (std::unique_ptr<KnobComponent>& knob,
                                             const juce::String& paramID,
                                             const juce::String& caption)
{
    knob = std::make_unique<KnobComponent> (caption);
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (
        processor.getAPVTS(), paramID, knob->getSlider()));
    addAndMakeVisible (*knob);
}

void VocalChopAudioProcessorEditor::rebuildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);

    auto* root = presetBox.getRootMenu();
    const auto all   = VocalChopAudioProcessor::getPresetNames();
    const int  chops = VocalChopAudioProcessor::getNumChopPresets();

    // Two kinds of preset, and the difference matters: the first group only
    // re-dresses whatever you already loaded, the second replaces the sound
    // outright. Unlabelled, testers assumed the menu was broken because
    // picking a preset never changed what they heard.
    int presetId = 1;
    root->addSectionHeader ("Chop FX - keeps your sample");
    for (int i = 0; i < all.size(); ++i)
    {
        if (i == chops)
        {
            root->addSeparator();
            root->addSectionHeader ("Sounds - loads an instrument");
        }
        presetBox.addItem (all[i], presetId++);
    }

    const auto mine = VocalChopAudioProcessor::getUserPresetNames();
    if (! mine.isEmpty())
    {
        root->addSeparator();
        root->addSectionHeader ("My presets");
        for (int i = 0; i < mine.size(); ++i)
            presetBox.addItem (mine[i], kUserPresetBaseId + i);
    }
    root->addSeparator();
    presetBox.addItem ("Save preset...", kSavePresetId);
}

void VocalChopAudioProcessorEditor::promptSavePreset()
{
    auto* aw = new juce::AlertWindow ("Save preset",
                                      "Name this sound - it will appear under "
                                      "My presets in every project.",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", "My sound", "Preset name");
    aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, aw] (int result)
        {
            const auto name = aw->getTextEditorContents ("name");
            std::unique_ptr<juce::AlertWindow> owner (aw);
            if (result != 1 || name.trim().isEmpty())
                return;

            if (processor.saveUserPreset (name))
            {
                rebuildPresetMenu();
                presetBox.setText (name.trim(), juce::dontSendNotification);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Slyce",
                    "Could not write the preset to\n"
                    + VocalChopAudioProcessor::userPresetFolder().getFullPathName());
            }
        }), false);
}

/** Mirrors the current voice into the hero, and greys it when the engine is
    not one that plays instruments. */
void VocalChopAudioProcessorEditor::setAmbient (bool on)
{
    if (on)
    {
        const auto cats = VocalChopAudioProcessor::getInstrumentCategories();
        const int idx = juce::jmax (0, instrumentBox.getSelectedItemIndex());
        ambientPanel.setNowPlaying (instrumentBox.getText(),
                                    juce::isPositiveAndBelow (idx, cats.size()) ? cats[idx]
                                                                                : juce::String(),
                                    chordBar.getKeyText());
        ambientPanel.setBpm (processor.getHostBpm());
        // Take focus so the arrow keys step between looks and Escape leaves.
        ambientPanel.toFront (true);
    }
    ambientPanel.setVisible (on);
    // The timer only turns while the panel can be seen. An ambient visualiser
    // that keeps repainting behind a closed door is the exact thing that makes
    // a plugin feel heavy for no reason.
    ambientPanel.setActive (on);
    if (! on) grabKeysSoon();
}

void VocalChopAudioProcessorEditor::refreshInstrumentHero()
{
    const auto& th = ThemeManager::active();
    const int idx = juce::jmax (0, instrumentBox.getSelectedItemIndex());
    const auto cats = VocalChopAudioProcessor::getInstrumentCategories();

    const bool live = instrumentBox.isEnabled();
    instNameLabel.setText (instrumentBox.getText(), juce::dontSendNotification);
    instNameLabel.setColour (juce::Label::textColourId,
                             live ? th.text : th.textSecondary.withAlpha (0.45f));

    juce::String sub;
    if (juce::isPositiveAndBelow (idx, cats.size()))
        sub = cats[idx];
    if (! live)
        sub = "Chop mode - the loaded sample plays across the keys";
    instCategoryLabel.setText (sub, juce::dontSendNotification);
    instCategoryLabel.setColour (juce::Label::textColourId,
                                 live ? th.accent : th.textSecondary.withAlpha (0.45f));

    for (auto& c : categoryChip) c.setEnabled (live);
    instBrowseButton.setEnabled (live);
    instPrevButton.setEnabled (live);
    instNextButton.setEnabled (live);

    for (int i = 0; i < 4; ++i)
        engineTab[i].setToggleState (i == engineBox.getSelectedItemIndex(),
                                     juce::dontSendNotification);
}

/** Selects the first instrument in `category`. */
void VocalChopAudioProcessorEditor::jumpToCategory (const juce::String& category)
{
    const auto cats = VocalChopAudioProcessor::getInstrumentCategories();
    for (int i = 0; i < cats.size(); ++i)
        if (cats[i] == category)
        {
            instrumentBox.setSelectedItemIndex (i, juce::sendNotificationSync);
            refreshInstrumentHero();
            grabKeysSoon();
            return;
        }
}

void VocalChopAudioProcessorEditor::stepInstrument (int delta)
{
    const int n = VocalChopAudioProcessor::getInstrumentNames().size();
    if (n <= 0)
        return;

    // Wrap, so holding the button walks off the end and back to the start
    // instead of stopping dead at "Init Synth".
    const int next = ((processor.getCurrentInstrument() + delta) % n + n) % n;
    processor.applyInstrument (next);

    // Ids in the menu are index+1; the featured shelf duplicates entries at
    // 1000+, so select by id rather than hunting for the matching text.
    instrumentBox.setSelectedId (next + 1, juce::dontSendNotification);

    // dontSendNotification is right - onChange would re-apply the instrument
    // we have just applied - but it also means nothing tells the hero to
    // update, so the name sat on the previous voice while the sound changed
    // underneath it. Refresh it here, explicitly.
    refreshInstrumentHero();

    grabKeysSoon();
}

void VocalChopAudioProcessorEditor::openFileChooser()
{
    const auto browserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    // In Sampled mode the Load button loads an SFZ multisample bank
    // (real recorded instruments: Salamander grand, acoustic guitars...).
    if (processor.isSamplerMode())
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Select an SFZ instrument (.sfz)", juce::File{}, "*.sfz");

        fileChooser->launchAsync (browserFlags, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;

            juce::String error;
            if (processor.loadSfzBank (file, error))
            {
                // Surface the fresh bank in the MY SAMPLES shelf right away.
                // Its id continues the 5000+ sequence and its path is
                // appended to the id->path table used by onChange.
                const auto rNames = VocalChopAudioProcessor::getRecentSfzNames();
                const auto rPaths = VocalChopAudioProcessor::getRecentSfzPaths();
                if (! rPaths.isEmpty() && ! mySamplePaths.contains (rPaths[0]))
                {
                    instrumentBox.addItem (rNames[0], 5000 + mySamplePaths.size());
                    mySamplePaths.add (rPaths[0]);
                }
                // Show the bank that is actually playing.
                const int sel = rPaths.isEmpty() ? -1 : mySamplePaths.indexOf (rPaths[0]);
                if (sel >= 0)
                    instrumentBox.setSelectedId (5000 + sel, juce::dontSendNotification);
            }
            else
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "SFZ load failed", error);
            grabKeysSoon();
        });
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser> (
        "Select an audio file to chop",
        juce::File{},
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    fileChooser->launchAsync (browserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        // In Melody mode a new sample should STAY melodic - snapping back
        // to Chop after every load forced a manual mode switch each time.
        if (file.existsAsFile()
            && processor.loadSampleFromFile (file, ! processor.isMelodyMode()))
        {
            syncSliceControls();
            refreshChildren();
            grabKeysSoon();
        }
    });
}

void VocalChopAudioProcessorEditor::applySlicing()
{
    auto& engine = processor.getSliceEngine();
    engine.setMode (sliceModeBox.getSelectedId() == 2 ? SliceEngine::Grid
                                                      : SliceEngine::Transient);
    engine.setGridDivision (gridBox.getSelectedId());
    engine.setSensitivity ((float) sensitivityKnob.getSlider().getValue());
    engine.rebuildSlices();
    // The selection points at a slice INDEX, and re-cutting changes what that
    // index means. Keeping it would leave the highlighted key and the
    // highlighted lane naming a different piece of audio than they did a
    // moment ago, which is precisely the thing the link exists to prevent.
    processor.setSelectedSlice (-1);
    refreshChildren();
}

void VocalChopAudioProcessorEditor::syncSliceControls()
{
    auto& engine = processor.getSliceEngine();
    sliceModeBox.setSelectedId (engine.getMode() == SliceEngine::Grid ? 2 : 1,
                                juce::dontSendNotification);

    const int div = engine.getGridDivision();
    if (div == 4 || div == 8 || div == 16 || div == 32)
        gridBox.setSelectedId (div, juce::dontSendNotification);

    syncEngineEnablement();
}

void VocalChopAudioProcessorEditor::syncEngineEnablement()
{
    // Half of this strip is dead in any given engine - WAVE and INSTRUMENT do
    // nothing while you are chopping a sample, and the slice controls do
    // nothing while you are playing the synth. Leaving them all lit is how a
    // tester ended up staring at "Supersaw Lead" in Chop mode wondering why
    // picking a sound changed nothing.
    const bool synth   = processor.isSynthMode();
    const bool sampler = processor.isSamplerMode();
    const bool melody  = processor.isMelodyMode();
    const bool chop    = ! synth;                    // Chop is engine 0

    // Chop with NOTHING loaded falls through to the synth (routeNoteOn keeps
    // the keys from ever being silent), so the instrument picker is live
    // there too - greying it out on a fresh instance would hide the only
    // control that makes a sound.
    const bool emptyChop = chop && processor.getSliceEngine().getNumSlices() == 0;
    const bool voice     = (synth && ! sampler && ! melody) || emptyChop;

    const bool byBeats = sliceModeBox.getSelectedId() == 2;

    const bool slicing = chop && ! emptyChop;
    sliceModeBox.setEnabled (slicing);
    gridBox.setEnabled      (slicing && byBeats);
    sensitivityKnob.setEnabled (slicing && ! byBeats);
    synthWaveBox.setEnabled  (voice);
    instrumentBox.setEnabled (voice);
    // The step arrows follow the picker they step. An earlier version had them
    // switch the engine to Synth so they always did something - which quietly
    // threw away a loaded chop, so they now simply go inert with the combo.
    instPrevButton.setEnabled (voice);
    instNextButton.setEnabled (voice);

    auto dim = [] (juce::Component& c, bool on)
    { c.setAlpha (on ? 1.0f : 0.38f); };

    dim (sliceModeBox,    slicing);
    dim (gridBox,         slicing && byBeats);
    dim (sensitivityKnob, slicing && ! byBeats);
    dim (synthWaveBox,    voice);
    dim (instrumentBox,   voice);
    dim (instPrevButton,  voice);
    dim (instNextButton,  voice);

    stripDimmed.clear();
    if (! slicing)             stripDimmed.insert ("SLICE BY");
    if (! (slicing && byBeats)) stripDimmed.insert ("GRID");
    if (! voice)             { stripDimmed.insert ("WAVE"); stripDimmed.insert ("INSTRUMENT"); }

    content.repaint (sliceCardBounds);
}

void VocalChopAudioProcessorEditor::refreshChildren()
{
    {
        const auto nm = processor.getLoadedSampleName();
        subtitleLabel.setText (nm.isEmpty() ? juce::String ("VOCAL CHOP INSTRUMENT")
                                            : nm.toUpperCase(),
                               juce::dontSendNotification);
    }

    resized();   // the artwork hero band depends on the active theme
    waveform.refresh();
    sliceGrid.refresh();
    chordBar.refreshKeyLabel();
    refreshInstrumentHero();
    repaint();
}

//==============================================================================
void VocalChopAudioProcessorEditor::grabKeysSoon()
{
    // Combo popups steal focus; take it back once they've closed so the
    // computer keyboard plays notes right away.
    juce::Component::SafePointer<VocalChopAudioProcessorEditor> safe (this);
    juce::Timer::callAfterDelay (80, [safe]
    {
        // Only when OUR window already has focus: this fires on automation /
        // project-load driven combo changes too, and yanking focus from a
        // host text field mid-typing turns keystrokes into notes.
        if (safe != nullptr && safe->isShowing()
            && safe->getPeer() != nullptr && safe->getPeer()->isFocused())
            safe->grabKeyboardFocus();
    });
}

void VocalChopAudioProcessorEditor::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

bool VocalChopAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // F2 toggles the visualiser. A shortcut matters here specifically because
    // ambient mode is the one screen you want to reach without hunting for a
    // button, and leave the same way.
    if (key.isKeyCode (juce::KeyPress::F2Key))
    {
        setAmbient (! ambientPanel.isVisible());
        return true;
    }

    // Swallow mapped musical keys (sound is driven by keyStateChanged, which
    // also sees releases); everything else passes through.
    const auto c = juce::CharacterFunctions::toLowerCase (
                       (juce::juce_wchar) key.getTextCharacter());
    return kTypingKeys.indexOfChar (c) >= 0;
}

bool VocalChopAudioProcessorEditor::scanTypingKeys (bool forceReleaseAll)
{
    bool handled = false;

    // New presses require our keyboard focus — otherwise typing in the
    // host's own text fields would play notes. hasKeyboardFocus(true) also
    // counts our own children though, so exclude text editors (knob value
    // boxes): typing "25" into Attack must not play C#4/F#4. Releases are
    // always honoured (that's the watchdog's whole job).
    auto* focusOwner = juce::Component::getCurrentlyFocusedComponent();
    const bool focused = hasKeyboardFocus (true)
                      && dynamic_cast<juce::TextEditor*> (focusOwner) == nullptr
                      && ! unlockPanel.isVisible()      // typing a license key
                      && ! welcomePanel.isVisible();    // reading the guide

    for (int i = 0; i < kTypingKeys.length(); ++i)
    {
        const bool down = ! forceReleaseAll && physicalKeyDown (kTypingKeys[i]);
        if (down == typingKeyHeld[(size_t) i])
            continue;
        if (down && ! focused)
            continue;

        typingKeyHeld[(size_t) i] = down;
        const int semitone = typingKeySemitone (i);

        if (down)
        {
            processor.pressSlicePad (semitone, 0.85f);
            sliceGrid.flashKey (semitone, 0.9f);
            spawnHeroFx (0.85f);
        }
        else
        {
            processor.releaseSlicePad (semitone);
        }
        handled = true;
    }

    return handled;
}

bool VocalChopAudioProcessorEditor::keyStateChanged (bool)
{
    return scanTypingKeys();
}

void VocalChopAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The processor restored its state behind our back (project revert,
    // generic preset switch). Attachments already updated the knobs; sync
    // everything else: slice combos, theme, instrument, cached waveform.
    syncSliceControls();
    themeBox.setSelectedId (ThemeManager::current() + 1, juce::dontSendNotification);
    // Guard like the constructor does: index 0 with no explicit pick means
    // "nothing chosen" (show the placeholder), and in Sampled mode the combo
    // is showing an SFZ bank the instrument list knows nothing about.
    if (processor.getCurrentInstrument() > 0 && ! processor.isSamplerMode())
        instrumentBox.setSelectedId (processor.getCurrentInstrument() + 1,
                                     juce::dontSendNotification);
    backdropTheme = -1;   // force the scene cache to re-render
    refreshChildren();
}

void VocalChopAudioProcessorEditor::timerCallback()
{
    // Watchdog: key releases are lost when focus moves mid-press (combo
    // popup, other window, host shortcut). isKeyCurrentlyDown reads global
    // OS key state, so this catches them within a frame. When the editor is
    // hidden entirely, let go of everything.
    scanTypingKeys (! isShowing());

    // Keep the waveform and the keyboard agreeing about which slice is
    // selected. Each sets it, neither knows about the other, so the editor is
    // the one place that can notice a change and repaint both.
    if (processor.getSelectedSlice() != lastSelectedSlice)
    {
        lastSelectedSlice = processor.getSelectedSlice();
        waveform.repaint();
        sliceGrid.repaint();
    }

    // Hide the UNLOCK affordance the moment activation succeeds.
    if (processor.isLicensed() && unlockButton.isVisible())
    {
        unlockButton.setVisible (false);
        content.repaint (juce::Rectangle<int> (0, 0, kBaseW, kBaseH).removeFromBottom (30));
    }

    // Advance the hero motion FX; repaint ONLY the artwork band, and only
    // while something is actually moving.
    if (! speedLines.empty() || heroGlow > 0.02f)
    {
        for (auto& s : speedLines)
        {
            s.x    -= s.speed;
            s.life -= 0.05f;
        }
        speedLines.erase (std::remove_if (speedLines.begin(), speedLines.end(),
                                          [] (const SpeedLine& s)
                                          { return s.life <= 0.0f || s.x + s.len < 0.0f; }),
                          speedLines.end());
        heroGlow *= 0.86f;

        if (! heroRect.isEmpty())
            content.repaint (heroRect);
    }
}

void VocalChopAudioProcessorEditor::spawnHeroFx (float velocity)
{
    if (heroRect.isEmpty() || ThemeManager::active().glow < 0.9f)
        return;

    const float w = (float) heroRect.getWidth();
    const float h = (float) heroRect.getHeight();

    const int count = 2 + fxRng.nextInt (2);
    for (int i = 0; i < count && speedLines.size() < 48; ++i)
    {
        SpeedLine s;
        s.y     = h * (0.30f + 0.58f * fxRng.nextFloat());
        s.len   = (70.0f + 130.0f * fxRng.nextFloat()) * juce::jmax (0.4f, velocity);
        s.x     = w * (0.55f + 0.55f * fxRng.nextFloat());
        s.speed = (w / 30.0f) * (0.55f + 0.75f * fxRng.nextFloat());
        s.life  = 1.0f;
        s.hue   = fxRng.nextInt (2);
        speedLines.push_back (s);
    }

    heroGlow = juce::jmin (1.0f, heroGlow + 0.45f + 0.4f * velocity);
}

void VocalChopAudioProcessorEditor::drawHeroFx (juce::Graphics& g)
{
    if (heroRect.isEmpty())
        return;
    if (speedLines.empty() && heroGlow <= 0.02f)
        return;

    const auto& theme = ThemeManager::active();
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (heroRect);

    // Glow pulse: headlights / wheels flare with the note, low in the band.
    if (heroGlow > 0.02f)
    {
        const float cx = heroRect.getWidth() * 0.5f;
        const float cy = heroRect.getHeight() * 0.62f;
        const float r  = heroRect.getWidth() * 0.30f;
        juce::ColourGradient pulse (theme.accent.withAlpha (0.10f * heroGlow), cx, cy,
                                    theme.accent.withAlpha (0.0f), cx + r, cy, true);
        g.setGradientFill (pulse);
        g.fillEllipse (cx - r, cy - r * 0.55f, r * 2.0f, r * 1.1f);
    }

    // Speed lines: the world rushing past — a thin bright core inside a
    // softer streak, fading along its own length.
    for (const auto& s : speedLines)
    {
        const auto col = (s.hue == 0 ? theme.accent : theme.waveform);
        juce::ColourGradient streak (col.withAlpha (0.0f), s.x, s.y,
                                     col.withAlpha (0.32f * s.life), s.x + s.len, s.y,
                                     false);
        g.setGradientFill (streak);
        g.fillRect (s.x, s.y - 1.0f, s.len, 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.20f * s.life));
        g.fillRect (s.x + s.len * 0.72f, s.y - 0.5f, s.len * 0.28f, 1.0f);
    }
}

//==============================================================================
void VocalChopAudioProcessorEditor::drawCard (juce::Graphics& g,
                                              juce::Rectangle<float> bounds) const
{
    const auto& theme = ThemeManager::active();
    const float radius = theme.cornerRadius;

    // Soft drop shadow — two cheap offset fills. (A gaussian DropShadow here
    // cost milliseconds PER CARD per paint and made the whole UI feel laggy.)
    g.setColour (theme.shadow.withAlpha (0.16f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 5.0f).expanded (2.0f), radius + 2.0f);
    g.setColour (theme.shadow.withAlpha (0.10f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f).expanded (0.5f), radius + 1.0f);

    // Material fill. On the glow theme the panels are darker glass so the
    // scene shows through without fighting the controls.
    if (theme.glow >= 0.9f)
    {
        g.setColour (juce::Colour (0xc008102a));
        g.fillRoundedRectangle (bounds, radius);

        // Faint neon rim — restrained: an expensive plugin whispers.
        g.setColour (theme.accent.withAlpha (0.06f));
        g.drawRoundedRectangle (bounds.expanded (1.0f), radius + 1.0f, 2.0f);
        g.setColour (theme.accent.withAlpha (0.16f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
        return;
    }

    g.setColour (theme.material);
    g.fillRoundedRectangle (bounds, radius);

    // Barely-there top light: the card reads as a raised surface, not a
    // flat rectangle. One linear gradient per card is cheap.
    {
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (theme.dark ? 0.035f : 0.25f),
                                    bounds.getX(), bounds.getY(),
                                    juce::Colours::white.withAlpha (0.0f),
                                    bounds.getX(), bounds.getY() + bounds.getHeight() * 0.45f,
                                    false);
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (bounds, radius);
    }

    // Hairline border + crisp 1px inner top highlight.
    g.setColour (theme.separator);
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (theme.dark ? 0.05f : 0.4f));
    g.fillRect (bounds.getX() + radius, bounds.getY() + 1.0f,
                bounds.getWidth() - radius * 2.0f, 1.0f);
}

juce::Rectangle<int> VocalChopAudioProcessorEditor::captioned (juce::Rectangle<int> cell,
                                                               const juce::String& caption)
{
    auto cap = cell.removeFromTop (12);
    if (caption.isNotEmpty())
        stripCaptions.push_back ({ caption, cap });
    return cell;
}

void VocalChopAudioProcessorEditor::drawStripCaptions (juce::Graphics& g) const
{
    const auto& theme = ThemeManager::active();
    g.setColour (theme.textSecondary.withAlpha (0.75f));
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Semibold"))
                   .withExtraKerningFactor (0.14f));

    for (const auto& c : stripCaptions)
    {
        const bool off = stripDimmed.count (c.first) > 0;
        g.setColour (theme.textSecondary.withAlpha (off ? 0.28f : 0.75f));
        g.drawText (c.first, c.second, juce::Justification::centred, false);
    }
}

void VocalChopAudioProcessorEditor::drawCaption (juce::Graphics& g,
                                                 const juce::String& text,
                                                 juce::Rectangle<int> cardBounds) const
{
    if (cardBounds.isEmpty())
        return;

    const auto& theme = ThemeManager::active();

    auto strip = cardBounds.reduced (kPadding, 0)
                           .removeFromTop (kCaptionH + 6)
                           .withTrimmedTop (6);

    // Small accent tick before the caption: ties every section to the
    // theme colour and gives the eye an anchor per card.
    g.setColour (theme.accent.withAlpha (0.85f));
    g.fillRoundedRectangle ((float) strip.getX(),
                            (float) strip.getCentreY() - 5.0f, 3.0f, 10.0f, 1.5f);

    g.setColour (theme.textSecondary);
    g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Semibold"))
                   .withExtraKerningFactor (0.08f));
    g.drawText (text.toUpperCase(), strip.withTrimmedLeft (9),
                juce::Justification::centredLeft);
}

void VocalChopAudioProcessorEditor::paint (juce::Graphics& g)
{
    // The scaled content canvas covers the window (fixed aspect ratio); this
    // only shows through for a frame during live-resize rounding.
    g.fillAll (ThemeManager::active().bgBottom);
}

void VocalChopAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();

    // Every theme now draws a designed backdrop, cached as an image so knob
    // repaints never re-render it.
    if (backdropCache.getWidth()  != kBaseW
     || backdropCache.getHeight() != kBaseH
     || backdropTheme != ThemeManager::current())
    {
        backdropCache = juce::Image (juce::Image::ARGB, kBaseW, kBaseH, true);
        juce::Graphics ig (backdropCache);

        if (theme.glow >= 0.9f)
            paintOceanScene (ig, kBaseW, kBaseH);      // the neon scene skin
        else
            paintStudioBackdrop (ig, kBaseW, kBaseH, theme);

        backdropTheme = ThemeManager::current();
    }
    g.drawImageAt (backdropCache, 0, 0);

    if (theme.glow >= 0.9f)
        drawHeroFx (g);

    // The instrument panel is the one place accent is used as decoration
    // rather than as state, because it is the hierarchy: this is what the eye
    // should land on first.
    if (! instCardBounds.isEmpty())
    {
        auto r = instCardBounds.toFloat();
        juce::ColourGradient hair (theme.accent.withAlpha (0.85f), r.getX(), r.getY(),
                                   theme.accent.withAlpha (0.0f), r.getRight(), r.getY(), false);
        g.setGradientFill (hair);
        g.fillRoundedRectangle (r.getX() + 1.0f, r.getY() + 0.5f,
                                r.getWidth() - 2.0f, 2.0f, 1.0f);
    }

    // --- Wordmark: accent-graded with a soft bloom (the brand focal point).
    {
        auto tb = titleLabel.getBounds().toFloat();
        if (! tb.isEmpty())
        {
            const auto font = juce::Font (juce::FontOptions (26.0f).withStyle ("Bold"))
                                  .withExtraKerningFactor (0.02f);
            g.setFont (font);

            // Bloom: a couple of offset passes in the accent, very low alpha.
            g.setColour (theme.accent.withAlpha (theme.glow >= 0.9f ? 0.30f : 0.16f));
            for (const auto d : { -1.6f, 1.6f })
                g.drawText ("slyce", tb.translated (d, 0.0f),
                            juce::Justification::centredLeft, false);
            g.drawText ("slyce", tb.translated (0.0f, 1.6f),
                        juce::Justification::centredLeft, false);

            juce::ColourGradient grad (theme.text, tb.getX(), tb.getY(),
                                       theme.text.interpolatedWith (theme.accent, 0.55f),
                                       tb.getX(), tb.getBottom(), false);
            g.setGradientFill (grad);
            g.drawText ("slyce", tb, juce::Justification::centredLeft, false);
        }
    }

    // Live label colours.
    titleLabel.setColour (juce::Label::textColourId, theme.text);
    subtitleLabel.setColour (juce::Label::textColourId, theme.textSecondary);
    presetLabel.setColour (juce::Label::textColourId, theme.textSecondary);
    octLabel.setColour (juce::Label::textColourId, theme.textSecondary);

    // Hairline under the toolbar.
    const auto full = juce::Rectangle<int> (0, 0, kBaseW, kBaseH).reduced (kMargin, 0);
    const int toolbarBottom = kMargin + kToolbarH + (kGap / 2);
    {
        juce::ColourGradient rule (theme.separator.withAlpha (0.0f), (float) full.getX(), 0.0f,
                                   theme.separator.withAlpha (0.0f), (float) full.getRight(), 0.0f,
                                   false);
        rule.addColour (0.5, theme.separator);
        g.setGradientFill (rule);
        g.fillRect ((float) full.getX(), (float) toolbarBottom, (float) full.getWidth(), 1.0f);
    }

    // Material cards. The LOOPER tab lies over every card except the slice
    // strip, and its own card has rounded corners - drawing the ones beneath
    // left ghost outlines poking out of those corners.
    drawCard (g, sliceCardBounds.toFloat());
    drawStripCaptions (g);
    if (! showLooper)
    {
        drawCard (g, engineCardBounds.toFloat());
        drawCard (g, instCardBounds.toFloat());
        drawCard (g, contextCardBounds.toFloat());
        drawCard (g, macroCardBounds.toFloat());
        drawCard (g, envCardBounds.toFloat());
        drawCard (g, toneCardBounds.toFloat());
        drawCard (g, synthCardBounds.toFloat());
        drawCard (g, filterCardBounds.toFloat());
        drawCard (g, playbackCardBounds.toFloat());
        drawCard (g, arpCardBounds.toFloat());

        // Section captions.
        drawCaption (g, "Engine",     engineCardBounds);
        drawCaption (g, "Instrument", instCardBounds);
        {
            // The context panel's header names the engine it belongs to, so
            // the two can never disagree about which mode you are in.
            static const char* ctxName[4] =
                { "Slicing", "Oscillator", "Sample bank", "Chromatic" };
            const int e = juce::jlimit (0, 3, engineBox.getSelectedItemIndex());
            drawCaption (g, ctxName[e], contextCardBounds);
        }
        drawCaption (g, "Macros",     macroCardBounds);
        drawCaption (g, "Envelope",   envCardBounds);
        drawCaption (g, "Pitch / Tone", toneCardBounds);
        drawCaption (g, "Synth",      synthCardBounds);
        drawCaption (g, "Filter",     filterCardBounds);
        drawCaption (g, "Playback",   playbackCardBounds);
        drawCaption (g, "Arp / Pump", arpCardBounds);
    }

    // Footer hint.
    g.setColour (theme.textSecondary);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    // ASCII only: char literals go through the wrong decoder on some
    // platforms and render as mojibake ("ar!" instead of a bullet).
    g.drawText (juce::String ("Play: MIDI / click keys / type Z S X D C V ...   |   drop audio to chop   |   v")
                    + JucePlugin_VersionString,
                juce::Rectangle<int> (0, 0, kBaseW, kBaseH).removeFromBottom (24)
                    .reduced (kMargin, 0),
                juce::Justification::centredRight);

    if (! processor.isLicensed())
    {
        g.setColour (juce::Colour (0xffff453a).withAlpha (0.85f));
        g.drawText ("DEMO - output mutes 2 s every minute",
                    juce::Rectangle<int> (kMargin + 104, kBaseH - 26, 300, 22),
                    juce::Justification::centredLeft);
    }
}

void VocalChopAudioProcessorEditor::resized()
{
    // Uniform scale: the fixed-size canvas fills the (aspect-locked) window.
    const float scale = juce::jmin (getWidth()  / (float) kBaseW,
                                    getHeight() / (float) kBaseH);
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, kBaseW, kBaseH);
    layoutContent();
    // The caption rectangles only exist after layout, and the dim state has
    // to survive every relayout (theme swap, looper toggle, host resize).
    syncEngineEnablement();
}

void VocalChopAudioProcessorEditor::layoutContent()
{
    auto area = juce::Rectangle<int> (0, 0, kBaseW, kBaseH).reduced (kMargin);

    // --- Top toolbar row ---
    auto top = area.removeFromTop (kToolbarH);
    titleLabel.setBounds (top.removeFromLeft (92));
    // 92 + 194 left + 690 right = 976 = exactly the space at the 1020 min
    // width; one more px and the LOOPER button lands on this caption.
    // The whole bar is sized to fit at the 1020px minimum width. Adding
    // AMBIENT without re-cutting the others put it straight on top of LOOPER,
    // so every element gave back what the new one needed.
    subtitleLabel.setBounds (top.removeFromLeft (128).withTrimmedTop (6));

    // Right-aligned: help, theme, load, demo, preset combo, preset label.
    helpButton.setBounds (top.removeFromRight (34).withSizeKeepingCentre (34, 34));
    top.removeFromRight (kGap / 2);
    themeBox.setBounds (top.removeFromRight (128).withSizeKeepingCentre (128, 34));
    top.removeFromRight (kGap / 2);
    loadButton.setBounds (top.removeFromRight (112).withSizeKeepingCentre (112, 34));
    top.removeFromRight (kGap / 2);
    demoButton.setBounds (top.removeFromRight (94).withSizeKeepingCentre (94, 34));
    top.removeFromRight (kGap / 2);
    presetBox.setBounds (top.removeFromRight (136).withSizeKeepingCentre (136, 34));
    presetLabel.setBounds (top.removeFromRight (46).withSizeKeepingCentre (46, 34));
    top.removeFromRight (kGap / 2);
    looperTabButton.setBounds (top.removeFromRight (78).withSizeKeepingCentre (78, 34));
    top.removeFromRight (5);
    ambientButton.setBounds (top.removeFromRight (78).withSizeKeepingCentre (78, 34));

    area.removeFromTop (kGap);

    // --- Artwork hero: on skin themes leave a clear band so the artwork is
    //     fully visible (grows with the window); modules live BELOW it. ---
    if (ThemeManager::active().glow >= 0.9f)
    {
        // 750 (not 690): the extra 60px goes to the knob rows below - with the
        // old value each dial had ~14px of height and simply vanished.
        const int bandH = juce::jlimit (110, 470, area.getHeight() - 796);
        heroRect = { 0, 0, kBaseW, area.getY() + bandH };   // motion-FX zone
        area.removeFromTop (bandH);
    }
    else
        heroRect = {};

    // --- Engine | Instrument | Context ------------------------------------
    // Three panels replacing what used to be one row of seven identical
    // combos. The old row had no hierarchy at all: the control that picks the
    // SOUND sat in a queue of look-alikes and the first tester could not find
    // it. Now the engine is a set of tabs you can read without opening
    // anything, the instrument is the biggest thing on the strip, and the
    // controls that only apply to the current engine live in their own panel
    // instead of being dimmed in place.
    auto strip = area.removeFromTop (152);
    sliceCardBounds = strip;
    stripCaptions.clear();
    {
        auto engineCard  = strip.removeFromLeft (212);
        strip.removeFromLeft (kGap);
        auto contextCard = strip.removeFromRight (274);
        strip.removeFromRight (kGap);
        auto instCard    = strip;

        engineCardBounds  = engineCard;
        instCardBounds    = instCard;
        contextCardBounds = contextCard;

        // ---- Engine: four vertical tabs ----------------------------------
        {
            auto in = engineCard.reduced (10, 9);
            in.removeFromTop (16);                     // room for the header
            const int th = in.getHeight() / 4;
            for (auto& t : engineTab)
                t.setBounds (in.removeFromTop (th).reduced (0, 1));
        }

        // ---- Instrument hero ---------------------------------------------
        {
            auto in = instCard.reduced (14, 9);
            in.removeFromTop (16);

            auto chipRow = in.removeFromBottom (24);
            in.removeFromBottom (6);

            // steppers and browse on the right, name fills what is left
            auto right = in.removeFromRight (150);
            {
                auto r = right.withSizeKeepingCentre (150, 40);
                instPrevButton.setBounds (r.removeFromLeft (34));
                r.removeFromLeft (4);
                instNextButton.setBounds (r.removeFromLeft (34));
                r.removeFromLeft (8);
                instBrowseButton.setBounds (r);
            }
            in.removeFromRight (10);

            instCategoryLabel.setBounds (in.removeFromTop (16));
            instNameLabel.setBounds     (in.removeFromTop (36));

            // The combo itself is never seen; it owns the categorised menu
            // that the Browse button opens, so there is one list to maintain.
            instrumentBox.setBounds (instBrowseButton.getBounds());

            const int cw = (chipRow.getWidth() - (kNumChips - 1) * 4) / kNumChips;
            for (auto& c : categoryChip)
            {
                c.setBounds (chipRow.removeFromLeft (cw));
                chipRow.removeFromLeft (4);
            }
        }

        // ---- Context: swaps with the engine rather than dimming ----------
        {
            const int eng = engineBox.getSelectedItemIndex();
            auto in = contextCard.reduced (12, 9);
            in.removeFromTop (16);
            const int capH = 15, rowH = 32;

            auto slot = [&] (int h, const juce::String& caption) -> juce::Rectangle<int>
            {
                auto row = in.removeFromTop (h);
                if (caption.isNotEmpty())
                    stripCaptions.push_back ({ caption, row.removeFromTop (capH) });
                return row;
            };

            const bool chop    = (eng == 0);
            const bool synth   = (eng == 1);
            const bool sampled = (eng == 2);
            const bool melody  = (eng == 3);

            sliceModeBox.setVisible    (chop);
            gridBox.setVisible         (chop);
            sensitivityKnob.setVisible (chop);
            synthWaveBox.setVisible    (synth);
            // Octave means something in every engine except Chop, where the
            // slices are laid out one per key and there is nothing to transpose.
            const bool showOct = ! chop;
            octDownButton.setVisible (showOct);
            octUpButton.setVisible   (showOct);
            octLabel.setVisible      (showOct);

            if (chop)
            {
                auto a = slot (capH + rowH, "SLICE BY");
                sliceModeBox.setBounds (a.withSizeKeepingCentre (a.getWidth(), rowH));
                in.removeFromTop (4);
                auto b = slot (capH + rowH + 12, "GRID");
                auto sens = b.removeFromRight (66);
                b.removeFromRight (8);
                gridBox.setBounds (b.withSizeKeepingCentre (b.getWidth(), rowH));
                sensitivityKnob.setBounds (sens);
            }
            else if (synth || melody || sampled)
            {
                if (synth)
                {
                    auto a = slot (capH + rowH, "WAVE");
                    synthWaveBox.setBounds (a.withSizeKeepingCentre (a.getWidth(), rowH));
                    in.removeFromTop (4);
                }
                auto o = slot (capH + rowH, "OCTAVE");
                auto oo = o.withSizeKeepingCentre (juce::jmin (150, o.getWidth()), rowH);
                octDownButton.setBounds (oo.removeFromLeft (36));
                octUpButton.setBounds   (oo.removeFromRight (36));
                octLabel.setBounds      (oo);
            }
        }
    }

    area.removeFromTop (kGap);

    // Reserve footer space.
    area.removeFromBottom (24 + kGap / 2);

    // --- Waveform row: waveform + meter column on the right ---
    auto waveRow = area.removeFromTop (juce::jmax (130, area.getHeight() * 21 / 100));
    const auto waveTop = waveRow;   // remembered for the LOOPER overlay
    meter.setBounds (waveRow.removeFromRight (kMeterW));
    waveRow.removeFromRight (kGap);
    waveform.setBounds (waveRow);

    area.removeFromTop (kGap);

    // --- Keyboard along the bottom, chord bar just above it ---
    auto sliceGridRow = area.removeFromBottom (juce::jmax (120, area.getHeight() * 28 / 100));
    sliceGrid.setBounds (sliceGridRow);
    area.removeFromBottom (kGap / 2);
    chordBar.setBounds (area.removeFromBottom (40));
    area.removeFromBottom (kGap);

    // --- Controls area: cards row (grouped) + FX rack side column ---
    auto controls = area;

    // The LOOPER tab covers the whole mid section (wave row through the
    // control cards); the keyboard below stays visible and playable.
    looperPanel.setBounds (juce::Rectangle<int> (waveTop.getX(), waveTop.getY(),
                                                 waveTop.getWidth(),
                                                 controls.getBottom() - waveTop.getY()));

    // FX rack as a side column on the right.
    auto fxCol = controls.removeFromRight (160);
    fxRack.setBounds (fxCol);
    controls.removeFromRight (kGap);

    // Remaining width split into labelled cards over three rows:
    // [Envelope | Pitch/Tone], [Synth modules], [Filter | Playback].
    // The first two rows are a single row of dials; the bottom row stacks
    // combos ON TOP of dials (filter type, arp/pump, playback modes), so an
    // even three-way split starved it - the dials collapsed to dots. Give it
    // 38% and split the remainder between the two simple rows.
    const int rowGap = kGap;
    const int freeH  = controls.getHeight() - rowGap * 2;
    const int rowH   = freeH * 29 / 100;
    auto topCards   = controls.removeFromTop (rowH);
    controls.removeFromTop (rowGap);
    auto synthCard  = controls.removeFromTop (rowH);
    controls.removeFromTop (rowGap);
    auto bottomCards = controls;
    synthCardBounds  = synthCard;

    auto layoutKnobRow = [] (juce::Rectangle<int> card, std::vector<KnobComponent*> knobs)
    {
        auto inner = card.reduced (kPadding, kPadding - 4);
        inner.removeFromTop (kCaptionH);        // room for the caption
        if (knobs.empty())
            return inner;

        // Size the dial band from the CELL WIDTH and centre it. A dial is
        // limited by whichever is smaller, so letting a narrow cell fill a
        // tall card just strands its label at the bottom with a hole above it.
        const int w = inner.getWidth() / (int) knobs.size();
        const int band = juce::jlimit (56, inner.getHeight(), w - 12 + 30);
        inner = inner.withSizeKeepingCentre (inner.getWidth(), band);
        for (auto* k : knobs)
            if (k != nullptr)
                k->setBounds (inner.removeFromLeft (w).reduced (6, 0));
        return inner;
    };

    // Top row: Envelope | Pitch/Tone.
    {
        const int gap = kGap;
        // Give Pitch/Tone a bit more width (5 knobs vs 4).
        auto envCard  = topCards.removeFromLeft ((topCards.getWidth() - gap) * 44 / 100);
        envCardBounds = envCard;
        topCards.removeFromLeft (gap);
        auto toneCard  = topCards;
        toneCardBounds = toneCard;

        layoutKnobRow (envCard,  { attackKnob.get(), decayKnob.get(),
                                   sustainKnob.get(), releaseKnob.get() });
        layoutKnobRow (toneCard, { pitchKnob.get(), formantKnob.get(), mixKnob.get(),
                                   widthKnob.get(), grainKnob.get(), detuneKnob.get() });
    }

    // Middle row: the synth architecture modules.
    layoutKnobRow (synthCard, { unisonKnob.get(), spreadKnob.get(), subKnob.get(),
                                noiseKnob.get(), fmKnob.get(), vibratoKnob.get(),
                                chorusKnob.get(), lfoRateKnob.get(), motionKnob.get(),
                                glideKnob.get() });

    // Bottom row: Filter | Macros | Playback.
    {
        const int gap = kGap;
        const int free = bottomCards.getWidth() - gap * 3;
        auto filterCard  = bottomCards.removeFromLeft (free * 16 / 100);
        filterCardBounds = filterCard;
        bottomCards.removeFromLeft (gap);

        // ARP + PUMP: the two tempo-locked performance engines.
        auto arpCard = bottomCards.removeFromLeft (free * 24 / 100);
        arpCardBounds = arpCard;
        bottomCards.removeFromLeft (gap);
        {
            // A 2x2 combo grid over a two-dial row. The dials keep whatever
            // height is left, so the card degrades gracefully when the window
            // is scaled down rather than collapsing them to nothing.
            auto inner = arpCard.reduced (kPadding - 4, kPadding - 4);
            inner.removeFromTop (kCaptionH);

            const int comboH = juce::jlimit (22, 28, inner.getHeight() / 6);
            const int cellH   = comboH + 12;
            auto grid = inner.removeFromTop (cellH * 2 + 2);

            auto row1 = grid.removeFromTop (cellH);
            arpModeBox.setBounds (captioned (row1.removeFromLeft (row1.getWidth() / 2)
                                                 .reduced (2, 0), "ARP"));
            arpRateBox.setBounds (captioned (row1.reduced (2, 0), "ARP RATE"));

            auto row2 = grid.removeFromBottom (cellH);
            arpOctBox.setBounds   (captioned (row2.removeFromLeft (row2.getWidth() / 2)
                                                  .reduced (2, 0), "OCTAVES"));
            pumpRateBox.setBounds (captioned (row2.reduced (2, 0), "PUMP RATE"));

            inner.removeFromTop (2);
            KnobComponent* ak[] = { arpGateKnob.get(), pumpKnob.get() };
            const int aw = inner.getWidth() / 2;
            for (auto* k : ak)
                if (k != nullptr)
                    k->setBounds (inner.removeFromLeft (aw).reduced (6, 0));
        }

        // HYPE / SPACE / DIRT are the "make it sound better" controls, meant
        // for someone who does not want to learn synthesis. They were the
        // SMALLEST dials on the panel, which is exactly backwards - so they
        // get their own wider card and the largest dials in the window.
        auto macroCard  = bottomCards.removeFromLeft (free * 31 / 100);
        macroCardBounds = macroCard;
        bottomCards.removeFromLeft (gap);
        {
            auto in = macroCard.reduced (kPadding, kPadding - 4);
            in.removeFromTop (kCaptionH);
            const int w = in.getWidth() / 3;
            KnobComponent* mk[3] = { hypeKnob.get(), spaceKnob.get(), dirtKnob.get() };
            for (auto* k : mk)
                if (k != nullptr)
                    k->setBounds (in.removeFromLeft (w).reduced (4, 0));
        }

        auto playbackCard  = bottomCards;
        playbackCardBounds = playbackCard;

        // Filter: two knobs then the type combo underneath.
        {
            auto inner = filterCard.reduced (kPadding, kPadding - 4);
            inner.removeFromTop (kCaptionH);
            auto comboRow = inner.removeFromBottom (46);
            filterTypeBox.setBounds (captioned (comboRow, "TYPE")
                                         .withSizeKeepingCentre (
                                             juce::jmin (220, comboRow.getWidth()), 32));
            inner.removeFromBottom (kGap / 2);

            KnobComponent* fk[] = { filterCutoffKnob.get(), filterResoKnob.get() };
            const int w = inner.getWidth() / 2;
            for (auto* k : fk)
                if (k != nullptr)
                    k->setBounds (inner.removeFromLeft (w).reduced (6, 0));
        }

        // Playback: toggles + play mode combo on the left, output knob on the right.
        {
            auto inner = playbackCard.reduced (kPadding, kPadding - 4);
            inner.removeFromTop (kCaptionH);

            auto knobCol = inner.removeFromRight (juce::jmin (192, inner.getWidth() / 2));
            {
                const int band = juce::jlimit (56, knobCol.getHeight(),
                                               knobCol.getWidth() / 2 - 12 + 30);
                knobCol = knobCol.withSizeKeepingCentre (knobCol.getWidth(), band);
            }
            const int kw = knobCol.getWidth() / 2;
            if (grainMixKnob != nullptr)
                grainMixKnob->setBounds (knobCol.removeFromLeft (kw).reduced (6, 0));
            if (outputGainKnob != nullptr)
                outputGainKnob->setBounds (knobCol.reduced (6, 0));
            inner.removeFromRight (kGap);

            auto controlsCol = inner;
            // Fit three rows into whatever height the card actually has -
            // fixed 30px rows overflowed and stacked on top of each other.
            // Four rows now (the delay-sync division joined the card), so the
            // row height follows the card instead of a fixed guess.
            const int btnH = juce::jlimit (18, 26, (controlsCol.getHeight() - 24) / 4 - 2);
            reverseButton.setBounds  (controlsCol.removeFromTop (btnH));
            controlsCol.removeFromTop (2);
            pingpongButton.setBounds (controlsCol.removeFromTop (btnH));
            controlsCol.removeFromTop (3);
            playModeBox.setBounds    (captioned (controlsCol.removeFromTop (btnH + 12),
                                                 "KEYS")
                                          .withSizeKeepingCentre (
                                              juce::jmin (200, controlsCol.getWidth()), btnH));
            controlsCol.removeFromTop (3);
            delaySyncBox.setBounds   (captioned (controlsCol.removeFromTop (btnH + 12),
                                                 "DELAY SYNC")
                                          .withSizeKeepingCentre (
                                              juce::jmin (200, controlsCol.getWidth()), btnH));
        }
    }

    // Licensing: footer button + full-window overlay.
    unlockButton.setBounds (kMargin, kBaseH - 26, 96, 22);
    unlockPanel.setBounds (0, 0, kBaseW, kBaseH);
    welcomePanel.setBounds (0, 0, kBaseW, kBaseH);
    ambientPanel.setBounds (0, 0, kBaseW, kBaseH);
}
