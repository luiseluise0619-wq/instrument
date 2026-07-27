#include "AmbientPanel.h"

namespace
{
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    const char* kLookNames[AmbientPanel::kNumLooks] = {
        "Flat bars", "Wavetable", "Tunnel", "Ribbon",
        "Grid", "Petals", "Globe", "Rain"
    };

    /** A deterministic hash, so every frame of a look is identical from the
        same index. Storing per-element state for 78 ribbon slats and 46 rain
        streaks would be a lot of memory for numbers that never change. */
    inline float h1 (int i, int salt = 0)
    {
        // Every step is UNSIGNED on purpose. Written with int multiplies, the
        // very first products overflow - 7 * 668265263 does not fit in an int -
        // and signed overflow is undefined, which an optimiser is entitled to
        // assume never happens. It compiled, ran, and hung the message thread
        // solid on the first frame that used it. Unsigned overflow is defined
        // to wrap, which is exactly what a hash wants.
        juce::uint32 x = (juce::uint32) i * 374761393u
                       + (juce::uint32) salt * 668265263u;
        x = (x ^ (x >> 13)) * 1274126177u;
        return (float) ((x ^ (x >> 16)) & 0xffffffu) / (float) 0xffffff;
    }

    /** Perspective projection. The design describes these looks as CSS 3D
        transforms; a divide by z is what those compile down to, and doing it
        directly means no per-frame layer compositing. */
    inline juce::Point<float> project (float x, float y, float z,
                                       juce::Point<float> centre, float d = 900.0f)
    {
        const float k = d / juce::jmax (1.0f, d + z);
        return { centre.x + x * k, centre.y + y * k };
    }
}

const char* AmbientPanel::lookName (int i)
{
    return kLookNames[juce::jlimit (0, kNumLooks - 1, i)];
}

AmbientPanel::AmbientPanel()
{
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);

    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step ( 1); };
    for (auto* b : { &prevButton, &nextButton })
    {
        b->setTooltip ("Previous / next look");
        addAndMakeVisible (*b);
    }

    lookLabel.setJustificationType (juce::Justification::centred);
    lookLabel.setInterceptsMouseClicks (false, false);
    lookLabel.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Medium")));
    addAndMakeVisible (lookLabel);

    for (int i = 0; i < kNumLooks; ++i)
    {
        dot[i].setButtonText ({});
        dot[i].setTooltip (kLookNames[i]);
        dot[i].onClick = [this, i] { look = i; phase = 0.0; lookLabel.setText (kLookNames[i], juce::dontSendNotification); repaint(); };
        addAndMakeVisible (dot[i]);
    }
    lookLabel.setText (kLookNames[0], juce::dontSendNotification);
}

AmbientPanel::~AmbientPanel() { stopTimer(); }

void AmbientPanel::setNowPlaying (juce::String voice, juce::String category, juce::String key)
{
    voiceName = voice; categoryName = category; keyName = key;
    if (isVisible()) repaint();
}

void AmbientPanel::setActive (bool shouldBeActive)
{
    if (shouldBeActive)
    {
        fadeIn = 0.0f;
        phase = 0.0;
        // 24 Hz. This is ambient motion - nothing in it needs to be smooth to
        // the frame - and every frame is a full-canvas repaint through a
        // scaled parent, which is the most expensive kind.
        sinceFullPaint = 0;
        startTimerHz (24);
        // Needed for the arrow keys and Escape. Safe here because setActive is
        // called after setVisible, so the panel is already on screen.
        grabKeyboardFocus();
    }
    else
        stopTimer();
}

void AmbientPanel::step (int delta)
{
    look = (look + delta + kNumLooks) % kNumLooks;
    phase = 0.0;
    lookLabel.setText (kLookNames[look], juce::dontSendNotification);
    repaint();
}

void AmbientPanel::timerCallback()
{
    phase += 1.0 / 24.0;
    fadeIn = juce::jmin (1.0f, fadeIn + 1.0f / 12.0f);   // 500 ms

    // Only the stage moves. The footer - wordmark, voice name, metadata - is
    // static between frames, and repainting it too means JUCE rescales the
    // whole 1080x1268 canvas through this panel's scaled parent thirty times
    // a second to redraw text that has not changed.
    if (fadeIn < 1.0f || ++sinceFullPaint > 48)
    {
        sinceFullPaint = 0;
        repaint();
    }
    else
        repaint (stageBounds);
}

void AmbientPanel::mouseDown (const juce::MouseEvent&)
{
    if (onExit) onExit();
}

bool AmbientPanel::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::leftKey))  { step (-1); return true; }
    if (k.isKeyCode (juce::KeyPress::rightKey)) { step ( 1); return true; }
    if (k.isKeyCode (juce::KeyPress::escapeKey)) { if (onExit) onExit(); return true; }
    return false;
}

void AmbientPanel::resized()
{
    auto r = getLocalBounds().reduced (22);
    auto top = r.removeFromTop (30);
    auto ctl = top.removeFromRight (200);
    prevButton.setBounds (ctl.removeFromLeft (30));
    nextButton.setBounds (ctl.removeFromRight (30));
    lookLabel.setBounds (ctl);

    auto dots = getLocalBounds().reduced (22).withTop (getLocalBounds().getY() + 60)
                    .withHeight (12).removeFromRight (kNumLooks * 18);
    dotRow = dots;
    for (auto& d : dot)
    {
        d.setBounds (dots.removeFromLeft (18).reduced (5, 2));
    }
}

//==============================================================================
void AmbientPanel::rebuildCaches()
{
    const auto& th = ThemeManager::active();
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());

    backdropCache = juce::Image (juce::Image::RGB, w, h, false);
    {
        juce::Graphics ig (backdropCache);
        ig.setGradientFill (juce::ColourGradient (th.bgTop, 0.0f, 0.0f,
                                                  th.bgBottom, 0.0f, (float) h, false));
        ig.fillAll();
    }

    // One 128px blob, stretched to whatever size a lobe needs. A soft radial
    // falloff is exactly the thing bilinear upscaling reproduces perfectly, so
    // the picture is the same and the cost is a blit instead of a shade.
    constexpr int kBlob = 128;
    blobCache = juce::Image (juce::Image::ARGB, kBlob, kBlob, true);
    {
        juce::Graphics ig (blobCache);
        ig.setGradientFill (juce::ColourGradient (
            th.accent.withAlpha (1.0f), kBlob * 0.5f, kBlob * 0.5f,
            juce::Colours::transparentBlack, (float) kBlob, kBlob * 0.5f, true));
        ig.fillEllipse (0.0f, 0.0f, (float) kBlob, (float) kBlob);
    }

    cachedTheme = ThemeManager::current();
}

void AmbientPanel::paint (juce::Graphics& g)
{
    const auto& th = ThemeManager::active();
    auto b = getLocalBounds().toFloat();
    if (b.isEmpty()) return;

    if (backdropCache.getWidth() != getWidth()
        || backdropCache.getHeight() != getHeight()
        || cachedTheme != ThemeManager::current())
        rebuildCaches();

    g.drawImageAt (backdropCache, 0, 0);

    {
        // Two lobes drifting a few percent over ~22 s, blitted from the cache.
        // One lobe, not two. The second was drawn underneath the first at 70%
        // of its size and 22% opacity - a 1200px scaled blit whose entire
        // contribution was hidden by the lobe on top of it.
        const float t = (float) phase;
        const float dx = std::sin (t * 0.285f) * b.getWidth() * 0.04f;
        const float dy = std::cos (t * 0.221f) * b.getHeight() * 0.04f;
        const juce::Point<float> c (b.getCentreX() + dx, b.getHeight() * 0.42f + dy);
        const float rad = b.getWidth() * 0.55f;
        g.setOpacity (0.26f * fadeIn);
        g.drawImage (blobCache,
                     juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c),
                     juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
    }

    // --- the look ----------------------------------------------------------
    auto stage = b.withTrimmedBottom (b.getHeight() * 0.30f);
    stageBounds = stage.getSmallestIntegerContainer();
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (b.toNearestInt());
        switch (look)
        {
            case 0: drawBars      (g, stage); break;
            case 1: drawWavetable (g, stage); break;
            case 2: drawTunnel    (g, stage); break;
            case 3: drawRibbon    (g, stage); break;
            case 4: drawGrid      (g, stage); break;
            case 5: drawPetals    (g, stage); break;
            case 6: drawGlobe     (g, stage); break;
            default: drawRain     (g, stage); break;
        }
    }


    // --- the bottom block --------------------------------------------------
    auto foot = b.removeFromBottom (b.getHeight() * 0.30f).reduced (34.0f, 20.0f);

    g.setColour (th.accent.withAlpha (0.9f * fadeIn));
    g.setFont (juce::Font (juce::FontOptions (17.0f).withStyle ("Bold")));
    g.drawText ("slyce", foot.removeFromTop (22.0f),
                juce::Justification::bottomLeft, false);

    foot.removeFromTop (6.0f);
    g.setColour (th.text.withAlpha (fadeIn));
    // The voice name is the point of the screen: set it as large as the panel
    // allows rather than at a fixed size, so it still dominates when the
    // window is dragged small.
    const float nameH = juce::jlimit (26.0f, 76.0f, foot.getHeight() * 0.52f);
    g.setFont (juce::Font (juce::FontOptions (nameH).withStyle ("Bold"))
                   .withExtraKerningFactor (-0.035f));
    g.drawText (voiceName, foot.removeFromTop (nameH * 1.16f),
                juce::Justification::centredLeft, false);

    // metadata row, separated by accent dots
    {
        auto row = foot.removeFromTop (20.0f);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        juce::StringArray bits { categoryName,
                                 juce::String (bpm > 1.0 ? bpm : 120.0, 1) + " BPM",
                                 keyName,
                                 th.name };
        float x = row.getX();
        for (int i = 0; i < bits.size(); ++i)
        {
            if (i > 0)
            {
                g.setColour (th.accent.withAlpha (0.85f * fadeIn));
                g.fillEllipse (x + 4.0f, row.getCentreY() - 2.0f, 4.0f, 4.0f);
                x += 14.0f;
            }
            g.setColour (th.textSecondary.withAlpha (fadeIn));
            // getStringWidth is deprecated in JUCE 8; GlyphArrangement is the
            // supported way to measure a run of text.
            juce::GlyphArrangement ga;
            ga.addLineOfText (g.getCurrentFont(), bits[i], 0.0f, 0.0f);
            const float w = ga.getBoundingBox (0, -1, true).getWidth();
            g.drawText (bits[i], juce::Rectangle<float> (x, row.getY(), w + 2.0f, row.getHeight()),
                        juce::Justification::centredLeft, false);
            x += w + 4.0f;
        }
    }

    g.setColour (th.textSecondary.withAlpha (0.55f * fadeIn));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("Click anywhere to return", foot.removeFromTop (16.0f),
                juce::Justification::centredLeft, false);

    // active dot
    for (int i = 0; i < kNumLooks; ++i)
    {
        auto d = dot[i].getBounds().toFloat().withSizeKeepingCentre (7.0f, 7.0f);
        g.setColour (i == look ? th.accent.withAlpha (fadeIn)
                               : th.textSecondary.withAlpha (0.35f * fadeIn));
        g.fillEllipse (d);
    }
}

//==============================================================================
// 1 — Flat bars: 52 bars, centre-weighted, each on its own slow cycle, with
//     three concentric rings turning behind them.
void AmbientPanel::drawBars (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& th = ThemeManager::active();
    const auto c = r.getCentre();
    const float t = (float) phase;

    for (int i = 0; i < 3; ++i)
    {
        const float rad = r.getHeight() * (0.22f + 0.10f * (float) i);
        const float spin = t * (i == 1 ? -0.105f : 0.070f);
        juce::Path ring;
        ring.addCentredArc (c.x, c.y, rad, rad, spin, 0.0f, kTwoPi * 0.82f, true);
        g.setColour (th.accent.withAlpha ((0.13f - 0.03f * (float) i) * fadeIn));
        g.strokePath (ring, juce::PathStrokeType (1.4f));
    }

    const float core = r.getHeight() * 0.055f * (1.0f + 0.06f * std::sin (t * 1.1f));
    g.setColour (th.accent.withAlpha (0.16f * fadeIn));
    g.fillEllipse (juce::Rectangle<float> (core * 2.0f, core * 2.0f).withCentre (c));

    constexpr int kBars = 52;
    const float bw = r.getWidth() / (kBars * 1.6f);
    for (int i = 0; i < kBars; ++i)
    {
        const float u = (float) i / (kBars - 1);
        // centre-weighted: the middle of the field is the tallest
        const float weight = 0.35f + 0.65f * std::sin (u * juce::MathConstants<float>::pi);
        const float cyc = 1.5f + h1 (i) * 2.4f;
        const float a = 0.28f + 0.72f * (0.5f + 0.5f * std::sin (t * kTwoPi / cyc + h1 (i, 7) * kTwoPi));
        const float hgt = r.getHeight() * 0.42f * weight * a;
        const float x = r.getX() + u * (r.getWidth() - bw);
        g.setColour (th.accent.withAlpha ((0.35f + 0.5f * a) * fadeIn));
        g.fillRoundedRectangle (x, c.y - hgt * 0.5f, bw, hgt, bw * 0.5f);
    }
}

// 2 — Wavetable: 26 polylines stacked into depth, rows bunching at the horizon.
void AmbientPanel::drawWavetable (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& th = ThemeManager::active();
    const float t = (float) phase;
    constexpr int kRows = 26, kPts = 90;

    for (int row = kRows - 1; row >= 0; --row)
    {
        const float d = (float) row / (kRows - 1);
        // y = 1 - d^0.78 packs the far rows together, which is what makes a
        // flat stack read as a receding plane.
        const float y = r.getBottom() - std::pow (d, 0.78f) * r.getHeight() * 0.90f;
        const float squash = 0.30f + 0.70f * (1.0f - d);
        const float amp = r.getHeight() * 0.055f * squash
                        * (0.62f + 0.66f * (0.5f + 0.5f * std::sin (
                              t * kTwoPi / (3.4f + h1 (row) * 3.2f) + h1 (row, 3) * kTwoPi)));

        juce::Path p;
        for (int i = 0; i < kPts; ++i)
        {
            const float u = (float) i / (kPts - 1);
            const float x = r.getCentreX() + (u - 0.5f) * r.getWidth() * squash;
            const float w = std::sin (u * kTwoPi * 2.0f + t * 0.8f + (float) row * 0.35f)
                          * std::sin (u * juce::MathConstants<float>::pi);
            const float yy = y + w * amp;
            if (i == 0) p.startNewSubPath (x, yy); else p.lineTo (x, yy);
        }
        g.setColour (th.accent.interpolatedWith (juce::Colours::white, 1.0f - d * 0.55f)
                              .withAlpha ((0.27f + 0.68f * (1.0f - d)) * fadeIn));
        g.strokePath (p, juce::PathStrokeType (0.9f + 1.7f * (1.0f - d)));
    }
}

// 3 — Tunnel: rings flying toward the viewer on a staggered 9 s cycle.
void AmbientPanel::drawTunnel (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& th = ThemeManager::active();
    const auto c = r.getCentre();
    const float t = (float) phase;
    constexpr int kRings = 22;

    for (int i = 0; i < kRings; ++i)
    {
        float u = std::fmod (t / 9.0f + (float) i / kRings, 1.0f);
        const float z = 1500.0f - u * 1920.0f;          // -1500 .. 420
        if (z <= -880.0f) continue;
        const auto p = project (r.getWidth() * 0.42f, 0.0f, z, c);
        const float rad = p.x - c.x;
        if (rad < 2.0f) continue;

        const float a = juce::jlimit (0.0f, 1.0f, u < 0.12f ? u / 0.12f
                                                            : juce::jmin (1.0f, (1.0f - u) * 6.0f));
        g.setColour (th.accent.withAlpha (a * 0.55f * fadeIn));
        g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c),
                       2.0f + 2.0f * u);
    }
}

// 4 — Ribbon: 78 slats on a circle, twisted into a Mobius band.
void AmbientPanel::drawRibbon (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& th = ThemeManager::active();
    const auto c = r.getCentre();
    const float t = (float) phase;
    const float R = juce::jmin (r.getWidth(), r.getHeight()) * 0.34f;
    const float spin = t * kTwoPi / 52.0f;
    constexpr int kSlats = 78;

    for (int i = 0; i < kSlats; ++i)
    {
        const float u = (float) i / kSlats;
        const float a = u * kTwoPi + spin;
        // The twist: the slat rotates half a turn for every turn of the band,
        // which is what closes it into one surface rather than two edges.
        const float twist = u * kTwoPi * 1.5f + spin * 1.5f;
        const float hgt = R * (0.16f + 0.10f * std::sin (u * kTwoPi * 2.0f));

        const float x = std::cos (a) * R, z = std::sin (a) * R;
        const float y = std::sin (twist) * hgt;
        const auto p1 = project (x, y, z, c);
        const auto p2 = project (x, -y, z, c);
        const float depth = 0.5f + 0.5f * (float) (z / R);
        g.setColour (th.accent.withAlpha ((0.15f + 0.60f * depth) * fadeIn));
        g.drawLine (p1.x, p1.y, p2.x, p2.y, 1.0f + 2.0f * depth);
    }
}

// 5 — Grid: two receding planes, scrolling in opposite directions.
void AmbientPanel::drawGrid (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& th = ThemeManager::active();
    const float t = (float) phase;
    const float horizon = r.getCentreY();

    for (int plane = 0; plane < 2; ++plane)
    {
        const float dir = plane == 0 ? 1.0f : -1.0f;
        const float scroll = std::fmod (t / (plane == 0 ? 2.6f : 3.4f), 1.0f);

        // depth lines
        for (int i = 0; i < 16; ++i)
        {
            const float u = std::fmod ((float) i / 16.0f + scroll, 1.0f);
            // 1/(1-u) spacing is the perspective: lines bunch at the horizon
            const float k = u * u;
            const float y = horizon + dir * k * r.getHeight() * 0.52f;
            g.setColour (th.accent.withAlpha (juce::jlimit (0.0f, 1.0f, k * 0.55f) * fadeIn));
            g.drawLine (r.getX(), y, r.getRight(), y, 1.0f);
        }
        // radial lines converging on the vanishing point
        for (int i = -8; i <= 8; ++i)
        {
            const float x = r.getCentreX() + (float) i * r.getWidth() * 0.14f;
            const float y = horizon + dir * r.getHeight() * 0.52f;
            g.setColour (th.accent.withAlpha (0.16f * fadeIn));
            g.drawLine (r.getCentreX(), horizon, x, y, 1.0f);
        }
    }

    // the glow sitting on the horizon
    g.setGradientFill (juce::ColourGradient (
        th.accent.withAlpha (0.30f * fadeIn), r.getCentreX(), horizon,
        juce::Colours::transparentBlack, r.getCentreX(), horizon - r.getHeight() * 0.22f, false));
    g.fillRect (r.withY (horizon - r.getHeight() * 0.22f).withHeight (r.getHeight() * 0.22f));
}

// 6 — Petals: 26 ellipses, each rotating and swelling on its own long cycle.
void AmbientPanel::drawPetals (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& th = ThemeManager::active();
    const auto c = r.getCentre();
    const float t = (float) phase;
    const float S = juce::jmin (r.getWidth(), r.getHeight());

    for (int i = 0; i < 26; ++i)
    {
        const float u = (float) i / 25.0f;
        const float cyc = 22.0f + h1 (i) * 65.0f;
        const float rot = t * kTwoPi / cyc + u * kTwoPi;
        const float swell = 1.0f + 0.14f * std::sin (t * kTwoPi / (cyc * 0.5f));
        const float w = S * (0.05f + u * 0.26f) * swell;
        const float hh = S * (0.26f + u * 0.36f) * swell;

        juce::Path p;
        p.addEllipse (-w * 0.5f, -hh * 0.5f, w, hh);
        p.applyTransform (juce::AffineTransform::rotation (rot).translated (c.x, c.y));
        g.setColour (th.accent.withAlpha (0.055f * fadeIn));
        g.fillPath (p);
        g.setColour (th.accent.withAlpha (0.10f * fadeIn));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
}

// 7 — Globe: 18 rings on a turning sphere.
void AmbientPanel::drawGlobe (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& th = ThemeManager::active();
    const auto c = r.getCentre();
    const float t = (float) phase;
    const float R = juce::jmin (r.getWidth(), r.getHeight()) * 0.34f;
    const float spin = t * kTwoPi / 40.0f;

    for (int i = 0; i < 18; ++i)
    {
        const float a = (float) i * juce::MathConstants<float>::pi / 18.0f + spin;
        // A ring seen edge-on is an ellipse whose width is its cosine. That is
        // the whole projection, and it costs one cosine per ring.
        const float w = std::abs (std::cos (a)) * R;
        const float depth = 0.35f + 0.65f * (0.5f + 0.5f * std::sin (a));
        g.setColour (th.accent.withAlpha (depth * 0.45f * fadeIn));
        g.drawEllipse (juce::Rectangle<float> (juce::jmax (1.0f, w * 2.0f), R * 2.0f)
                           .withCentre (c), 1.5f);
    }
    g.setColour (th.accent.withAlpha (0.22f * fadeIn));
    g.drawEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c), 1.0f);
}

// 8 — Rain: 46 streaks at varied depths, stretching as they fall.
void AmbientPanel::drawRain (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& th = ThemeManager::active();
    const float t = (float) phase;

    for (int i = 0; i < 46; ++i)
    {
        const float dur = 2.6f + h1 (i) * 2.8f;
        const float u = std::fmod ((t + h1 (i, 11) * dur) / dur, 1.0f);
        const float depth = 0.25f + h1 (i, 5) * 0.75f;
        const float x = r.getX() + h1 (i, 2) * r.getWidth();
        const float y = r.getY() - r.getHeight() * 0.20f + u * r.getHeight() * 1.40f;
        const float len = r.getHeight() * (0.05f + 0.10f * u) * depth;

        g.setColour (th.accent.withAlpha (depth * 0.55f * (1.0f - u * 0.35f) * fadeIn));
        g.drawLine (x, y, x, y + len, 0.8f + 1.8f * depth);
    }
}
