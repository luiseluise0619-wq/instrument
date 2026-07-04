#include "PluginEditor.h"
#include "UI/ThemeManager.h"
#include "BinaryData.h"

namespace
{
    // Consistent outer margin / inner padding for the Apple-style layout.
    constexpr int kMargin  = 22;
    constexpr int kPadding = 18;
    constexpr int kGap     = 16;

    // Toolbar / caption metrics.
    constexpr int kToolbarH   = 34;
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

    // Full-bleed artwork skin: cover-scale the embedded PNG, then darken it
    // enough that the glass panels and text stay readable on top.
    void paintArtworkBackdrop (juce::Graphics& g, int wi, int hi,
                               const void* data, int dataSize)
    {
        const float w = (float) wi;
        const float h = (float) hi;

        const auto img = juce::ImageCache::getFromMemory (data, dataSize);

        if (img.isValid())
        {
            g.drawImage (img, { 0.0f, 0.0f, w, h },
                         juce::RectanglePlacement (juce::RectanglePlacement::fillDestination
                                                   | juce::RectanglePlacement::centred));
        }
        else
        {
            g.fillAll (juce::Colour (0xff05030f));
        }

        // Global dim so neon controls read as the brightest thing on screen.
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRect (0.0f, 0.0f, w, h);

        // Extra darkening where the dense control cards live (lower 2/3).
        {
            juce::ColourGradient shade (juce::Colours::transparentBlack, 0.0f, h * 0.30f,
                                        juce::Colour (0xff05030f).withAlpha (0.72f),
                                        0.0f, h, false);
            g.setGradientFill (shade);
            g.fillRect (0.0f, h * 0.30f, w, h * 0.70f);
        }

        // Soft top band behind the toolbar.
        {
            juce::ColourGradient top (juce::Colours::black.withAlpha (0.45f), 0.0f, 0.0f,
                                      juce::Colours::transparentBlack, 0.0f, 80.0f, false);
            g.setGradientFill (top);
            g.fillRect (0.0f, 0.0f, w, 80.0f);
        }

        // Brand-cohesive CRT scanlines, very subtle.
        g.setColour (juce::Colours::black.withAlpha (0.05f));
        for (float sy = 0.0f; sy < h; sy += 3.0f)
            g.fillRect (0.0f, sy, w, 1.0f);
    }

    // FL-style typing keys: bottom row = C3 octave, top row = C4 octave.
    // ',' is deliberately NOT mapped: it would duplicate Q's C4, and two keys
    // driving one note means releasing either kills the other's sound.
    const juce::String kTypingKeys ("zsxdcvgbhnjmq2w3er5t6y7u");

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

    // --- Title ---
    titleLabel.setText ("VOCALCHOP STUDIO", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (19.0f).withStyle ("Semibold"))
                            .withExtraKerningFactor (0.14f));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    // --- Preset menu (NOT an APVTS param) ---
    presetLabel.setText ("Preset", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (presetLabel);

    {
        int presetId = 1;
        for (const auto& name : VocalChopAudioProcessor::getPresetNames())
            presetBox.addItem (name, presetId++);
        presetBox.setSelectedId (1, juce::dontSendNotification);
        presetBox.setJustificationType (juce::Justification::centred);
        presetBox.onChange = [this]
        {
            processor.applyPreset (presetBox.getSelectedId() - 1);
            refreshChildren();
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
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getAPVTS(), "engine", engineBox));
    engineBox.onChange = [this] { refreshChildren(); grabKeysSoon(); };
    addAndMakeVisible (engineBox);

    synthWaveBox.addItem ("Saw", 1);
    synthWaveBox.addItem ("Square", 2);
    synthWaveBox.addItem ("Sine", 3);
    synthWaveBox.addItem ("Triangle", 4);
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getAPVTS(), "synthWave", synthWaveBox));
    addAndMakeVisible (synthWaveBox);

    // --- Instrument picker: designed synth patches (switches to Synth mode),
    //     grouped by category with section headings ---
    {
        const auto names = VocalChopAudioProcessor::getInstrumentNames();
        const auto cats  = VocalChopAudioProcessor::getInstrumentCategories();
        juce::String lastCat;
        for (int i = 0; i < names.size(); ++i)
        {
            if (cats[i] != lastCat)
            {
                instrumentBox.addSectionHeading (cats[i]);
                lastCat = cats[i];
            }
            instrumentBox.addItem (names[i], i + 1);
        }
    }
    instrumentBox.setTextWhenNothingSelected ("Instrument");
    if (processor.getCurrentInstrument() > 0)
        instrumentBox.setSelectedId (processor.getCurrentInstrument() + 1,
                                     juce::dontSendNotification);
    instrumentBox.onChange = [this]
    {
        if (instrumentBox.getSelectedId() > 0)
            processor.applyInstrument (instrumentBox.getSelectedId() - 1);
        refreshChildren();
        grabKeysSoon();   // pick a patch, play it immediately
    };
    addAndMakeVisible (instrumentBox);

    addAndMakeVisible (chordBar);

    // --- Slice mode (initialised from the engine so restored state shows) ---
    auto& engine = processor.getSliceEngine();

    sliceModeBox.addItem ("Transient", 1);
    sliceModeBox.addItem ("Grid", 2);
    sliceModeBox.setSelectedId (engine.getMode() == SliceEngine::Grid ? 2 : 1,
                                juce::dontSendNotification);
    sliceModeBox.setJustificationType (juce::Justification::centred);
    sliceModeBox.onChange = [this] { applySlicing(); };
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

    setResizable (true, true);
    // Minimum width must fit the fixed slice-control row (engine + slicing
    // combos + wave + instrument + octave buttons) without clipping.
    setResizeLimits (1020, 760, 1800, 1400);
    setSize (1080, 900);

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

void VocalChopAudioProcessorEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Select an audio file to chop",
        juce::File{},
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile() && processor.loadSampleFromFile (file))
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
}

void VocalChopAudioProcessorEditor::refreshChildren()
{
    waveform.refresh();
    sliceGrid.refresh();
    repaint();
}

//==============================================================================
void VocalChopAudioProcessorEditor::grabKeysSoon()
{
    // Combo popups steal focus; take it back once they've closed so the
    // computer keyboard plays notes right away.
    juce::Component::SafePointer<VocalChopAudioProcessorEditor> safe (this);
    juce::Timer::callAfterDelay (120, [safe]
    {
        if (safe != nullptr && safe->isShowing())
            safe->grabKeyboardFocus();
    });
}

void VocalChopAudioProcessorEditor::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

bool VocalChopAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
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
                      && dynamic_cast<juce::TextEditor*> (focusOwner) == nullptr;

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
}

//==============================================================================
void VocalChopAudioProcessorEditor::drawCard (juce::Graphics& g,
                                              juce::Rectangle<float> bounds) const
{
    const auto& theme = ThemeManager::active();
    const float radius = theme.cornerRadius;

    // Soft drop shadow.
    {
        juce::DropShadow shadow (theme.shadow.withAlpha (0.35f), 18, { 0, 6 });
        juce::Path p;
        p.addRoundedRectangle (bounds, radius);
        shadow.drawForPath (g, p);
    }

    // Material fill. On the glow theme the panels are darker glass so the
    // scene shows through without fighting the controls.
    if (theme.glow >= 0.9f)
    {
        g.setColour (juce::Colour (0xc008102a));
        g.fillRoundedRectangle (bounds, radius);

        // Faint neon rim.
        g.setColour (theme.accent.withAlpha (0.10f));
        g.drawRoundedRectangle (bounds.expanded (1.0f), radius + 1.0f, 2.5f);
        g.setColour (theme.accent.withAlpha (0.28f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
        return;
    }

    g.setColour (theme.material);
    g.fillRoundedRectangle (bounds, radius);

    // Hairline border.
    g.setColour (theme.separator);
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

void VocalChopAudioProcessorEditor::drawCaption (juce::Graphics& g,
                                                 const juce::String& text,
                                                 juce::Rectangle<int> cardBounds) const
{
    if (cardBounds.isEmpty())
        return;

    const auto& theme = ThemeManager::active();
    g.setColour (theme.textSecondary);
    g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Semibold")));

    auto strip = cardBounds.reduced (kPadding, 0)
                           .removeFromTop (kCaptionH + 6)
                           .withTrimmedTop (6);
    g.drawText (text.toUpperCase(), strip, juce::Justification::centredLeft);
}

void VocalChopAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();

    if (theme.glow >= 0.9f)
    {
        // Full-glow default theme: the Ocean Pluck scene, cached so knob
        // repaints don't re-render the artwork.
        if (backdropCache.getWidth()  != getWidth()
         || backdropCache.getHeight() != getHeight()
         || backdropTheme != ThemeManager::current())
        {
            backdropCache = juce::Image (juce::Image::ARGB,
                                         juce::jmax (1, getWidth()),
                                         juce::jmax (1, getHeight()), true);
            juce::Graphics ig (backdropCache);

            const juce::String themeName (theme.name);
            if (themeName == "Neon Rider")
                paintArtworkBackdrop (ig, getWidth(), getHeight(),
                                      BinaryData::skin_neon_rider_png,
                                      BinaryData::skin_neon_rider_pngSize);
            else if (themeName == "Neo-Seoul")
                paintArtworkBackdrop (ig, getWidth(), getHeight(),
                                      BinaryData::skin_neo_seoul_png,
                                      BinaryData::skin_neo_seoul_pngSize);
            else
                paintOceanScene (ig, getWidth(), getHeight());

            backdropTheme = ThemeManager::current();
        }
        g.drawImageAt (backdropCache, 0, 0);
    }
    else
    {
        juce::ColourGradient bg (theme.bgTop, 0.0f, 0.0f,
                                 theme.bgBottom, 0.0f, (float) getHeight(), false);
        g.setGradientFill (bg);
        g.fillAll();
    }

    // Live label colours.
    titleLabel.setColour (juce::Label::textColourId, theme.text);
    presetLabel.setColour (juce::Label::textColourId, theme.textSecondary);
    octLabel.setColour (juce::Label::textColourId, theme.textSecondary);

    // Hairline under the toolbar.
    const auto full = getLocalBounds().reduced (kMargin, 0);
    const int toolbarBottom = kMargin + kToolbarH + (kGap / 2);
    g.setColour (theme.separator);
    g.fillRect (full.getX(), toolbarBottom, full.getWidth(), 1);

    // Material cards.
    drawCard (g, sliceCardBounds.toFloat());
    drawCard (g, envCardBounds.toFloat());
    drawCard (g, toneCardBounds.toFloat());
    drawCard (g, synthCardBounds.toFloat());
    drawCard (g, filterCardBounds.toFloat());
    drawCard (g, playbackCardBounds.toFloat());

    // Section captions.
    drawCaption (g, "Envelope",   envCardBounds);
    drawCaption (g, "Pitch / Tone", toneCardBounds);
    drawCaption (g, "Synth",      synthCardBounds);
    drawCaption (g, "Filter",     filterCardBounds);
    drawCaption (g, "Playback",   playbackCardBounds);

    // Footer hint.
    g.setColour (theme.textSecondary);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (juce::String ("Play: MIDI / click keys / type Z S X D C V ...   •   drop audio to chop   •   v")
                    + JucePlugin_VersionString,
                getLocalBounds().removeFromBottom (24).reduced (kMargin, 0),
                juce::Justification::centredRight);
}

void VocalChopAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (kMargin);

    // --- Top toolbar row ---
    auto top = area.removeFromTop (kToolbarH);
    titleLabel.setBounds (top.removeFromLeft (300));

    // Right-aligned: theme, load, demo, preset combo, preset label.
    themeBox.setBounds (top.removeFromRight (150).withSizeKeepingCentre (150, 30));
    top.removeFromRight (kGap / 2);
    loadButton.setBounds (top.removeFromRight (130).withSizeKeepingCentre (130, 30));
    top.removeFromRight (kGap / 2);
    demoButton.setBounds (top.removeFromRight (70).withSizeKeepingCentre (70, 30));
    top.removeFromRight (kGap / 2);
    presetBox.setBounds (top.removeFromRight (160).withSizeKeepingCentre (160, 30));
    presetLabel.setBounds (top.removeFromRight (56).withSizeKeepingCentre (56, 30));

    area.removeFromTop (kGap);

    // --- Slice control card ---
    auto sliceCard = area.removeFromTop (94);
    sliceCardBounds = sliceCard;
    {
        auto inner = sliceCard.reduced (kPadding, kPadding - 4);
        engineBox.setBounds (inner.removeFromLeft (110).withSizeKeepingCentre (110, 30));
        inner.removeFromLeft (kGap);
        sliceModeBox.setBounds (inner.removeFromLeft (130).withSizeKeepingCentre (130, 30));
        inner.removeFromLeft (kGap);
        gridBox.setBounds (inner.removeFromLeft (120).withSizeKeepingCentre (120, 30));
        inner.removeFromLeft (kGap);
        sensitivityKnob.setBounds (inner.removeFromLeft (90));
        inner.removeFromLeft (kGap);
        synthWaveBox.setBounds (inner.removeFromLeft (120).withSizeKeepingCentre (120, 30));
        inner.removeFromLeft (kGap);
        instrumentBox.setBounds (inner.removeFromLeft (160).withSizeKeepingCentre (160, 30));
        inner.removeFromLeft (kGap);
        octDownButton.setBounds (inner.removeFromLeft (30).withSizeKeepingCentre (30, 30));
        octLabel.setBounds      (inner.removeFromLeft (52).withSizeKeepingCentre (52, 30));
        octUpButton.setBounds   (inner.removeFromLeft (30).withSizeKeepingCentre (30, 30));
    }

    area.removeFromTop (kGap);

    // Reserve footer space.
    area.removeFromBottom (24 + kGap / 2);

    // --- Waveform row: waveform + meter column on the right ---
    auto waveRow = area.removeFromTop (juce::jmax (150, area.getHeight() * 30 / 100));
    meter.setBounds (waveRow.removeFromRight (kMeterW));
    waveRow.removeFromRight (kGap);
    waveform.setBounds (waveRow);

    area.removeFromTop (kGap);

    // --- Keyboard along the bottom, chord bar just above it ---
    auto sliceGridRow = area.removeFromBottom (juce::jmax (120, area.getHeight() * 32 / 100));
    sliceGrid.setBounds (sliceGridRow);
    area.removeFromBottom (kGap / 2);
    chordBar.setBounds (area.removeFromBottom (36));
    area.removeFromBottom (kGap);

    // --- Controls area: cards row (grouped) + FX rack side column ---
    auto controls = area;

    // FX rack as a side column on the right.
    auto fxCol = controls.removeFromRight (160);
    fxRack.setBounds (fxCol);
    controls.removeFromRight (kGap);

    // Remaining width split into labelled cards over three rows:
    // [Envelope | Pitch/Tone], [Synth modules], [Filter | Playback].
    const int rowGap = kGap;
    const int rowH   = (controls.getHeight() - rowGap * 2) / 3;
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
        const int w = inner.getWidth() / (int) knobs.size();
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
                                chorusKnob.get() });

    // Bottom row: Filter | Playback.
    {
        const int gap = kGap;
        auto filterCard  = bottomCards.removeFromLeft ((bottomCards.getWidth() - gap) * 42 / 100);
        filterCardBounds = filterCard;
        bottomCards.removeFromLeft (gap);
        auto playbackCard  = bottomCards;
        playbackCardBounds = playbackCard;

        // Filter: two knobs then the type combo underneath.
        {
            auto inner = filterCard.reduced (kPadding, kPadding - 4);
            inner.removeFromTop (kCaptionH);
            auto comboRow = inner.removeFromBottom (36);
            filterTypeBox.setBounds (comboRow.withSizeKeepingCentre (
                juce::jmin (220, comboRow.getWidth()), 30));
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
            const int kw = knobCol.getWidth() / 2;
            if (grainMixKnob != nullptr)
                grainMixKnob->setBounds (knobCol.removeFromLeft (kw).reduced (6, 0));
            if (outputGainKnob != nullptr)
                outputGainKnob->setBounds (knobCol.reduced (6, 0));
            inner.removeFromRight (kGap);

            auto controlsCol = inner;
            const int rowH = 30;
            reverseButton.setBounds  (controlsCol.removeFromTop (rowH));
            controlsCol.removeFromTop (kGap / 2);
            pingpongButton.setBounds (controlsCol.removeFromTop (rowH));
            controlsCol.removeFromTop (kGap / 2);
            playModeBox.setBounds    (controlsCol.removeFromTop (rowH)
                                          .withSizeKeepingCentre (
                                              juce::jmin (200, controlsCol.getWidth()), 30));
        }
    }
}
