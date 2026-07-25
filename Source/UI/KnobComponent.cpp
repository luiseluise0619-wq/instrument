#include "KnobComponent.h"
#include "ThemeManager.h"

//==============================================================================
void KnobComponent::KnobLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& slider)
{
    const auto& theme = ThemeManager::active();

    // Leave a little breathing room for the shadow and arc.
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto centre  = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Disc slightly inset from the arc rings.
    const float discRadius = radius * 0.82f;
    auto discBounds = juce::Rectangle<float> (discRadius * 2.0f, discRadius * 2.0f).withCentre (centre);

    const bool  hover = slider.isMouseOverOrDragging();
    const float trail = (float) slider.getProperties()
                                      .getWithDefault ("dragGlow", 0.0f);

    //--------------------------------------------------------------------------
    // (a0) Ambient neon bloom behind the whole knob — the control itself
    //      reads as a light source. Flares while dragging / hovering.
    const bool mini = radius < 24.0f;   // tiny cells: crisp dial, no light show

    if (theme.glow >= 0.5f && ! mini)
    {
        const float bloomR = radius * 1.45f;
        const float bloomA = juce::jlimit (0.0f, 0.45f,
                                           0.10f + 0.14f * trail + (hover ? 0.07f : 0.0f));
        juce::ColourGradient bloom (theme.accent.withAlpha (bloomA * theme.glow),
                                    centre.x, centre.y,
                                    juce::Colours::transparentBlack,
                                    centre.x + bloomR, centre.y, true);
        g.setGradientFill (bloom);
        g.fillEllipse (juce::Rectangle<float> (bloomR * 2.0f, bloomR * 2.0f)
                           .withCentre (centre));
    }

    //--------------------------------------------------------------------------
    // (a) Soft drop shadow beneath the disc.
    if (! mini)
    {
        juce::DropShadow shadow (theme.shadow, 8, { 0, 2 });
        juce::Path discPath;
        discPath.addEllipse (discBounds);
        shadow.drawForPath (g, discPath);
    }

    //--------------------------------------------------------------------------
    // (b) Base disc. Apple keeps chrome neutral: a light grey face, shaded
    // top-to-bottom, with the colour reserved for the value arc outside it.
    {
        const auto base = theme.glow >= 0.9f
                            ? theme.materialStrong.withAlpha (1.0f).brighter (0.25f)
                            : theme.control;

        juce::ColourGradient face (base.brighter (theme.dark ? 0.20f : 0.02f),
                                   centre.x, discBounds.getY(),
                                   base.darker (theme.dark ? 0.26f : 0.10f),
                                   centre.x, discBounds.getBottom(), false);
        g.setGradientFill (face);
        g.fillEllipse (discBounds);

        // Fine bevel: a bright arc across the top edge, a dark one beneath.
        juce::Path topArc, botArc;
        const auto inner = discBounds.reduced (0.6f);
        // 0 rad is 12 o'clock and positive runs clockwise: the bright arc must
        // span the TOP (-95 deg .. +95 deg) and the dark one the BOTTOM.
        topArc.addCentredArc (centre.x, centre.y, inner.getWidth() * 0.5f,
                              inner.getHeight() * 0.5f, 0.0f,
                              -juce::MathConstants<float>::halfPi * 0.95f,
                              juce::MathConstants<float>::halfPi * 0.95f, true);
        botArc.addCentredArc (centre.x, centre.y, inner.getWidth() * 0.5f,
                              inner.getHeight() * 0.5f, 0.0f,
                              juce::MathConstants<float>::halfPi * 1.05f,
                              juce::MathConstants<float>::halfPi * 2.95f, true);
        g.setColour (juce::Colours::white.withAlpha (theme.dark ? 0.13f : 0.55f));
        g.strokePath (topArc, juce::PathStrokeType (1.1f));
        g.setColour (juce::Colours::black.withAlpha (theme.dark ? 0.28f : 0.10f));
        g.strokePath (botArc, juce::PathStrokeType (1.1f));
    }

    // (f) Very subtle glassy top inner highlight (dark themes only).
    if (theme.dark)
    {
        juce::ColourGradient glass (juce::Colours::white.withAlpha (0.10f),
                                    centre.x, discBounds.getY(),
                                    juce::Colours::white.withAlpha (0.0f),
                                    centre.x, centre.y, false);
        g.setGradientFill (glass);
        g.fillEllipse (discBounds.reduced (1.0f));
    }

    // Rim on the disc: neon-tinted on glow themes, hairline elsewhere.
    if (theme.glow >= 0.9f)
        g.setColour (theme.accent.withAlpha (0.60f + 0.25f * trail));
    else
        g.setColour (theme.separator);
    g.drawEllipse (discBounds, theme.glow >= 0.9f ? 1.2f : 1.0f);

    //--------------------------------------------------------------------------
    // Ring geometry (sits just outside the disc).
    const float ringRadius = radius - 2.0f;
    const float ringThickness = juce::jmax (2.5f, radius * 0.10f);

    // (c) Thin inactive track ring.
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (theme.glow >= 0.9f ? theme.accent.withAlpha (0.22f)
                                        : theme.controlTrack);
        g.strokePath (track, juce::PathStrokeType (ringThickness,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // (d) LED-ring progress arc with a neon halo and cyan->purple gradient.
    if (sliderPos > 0.0f)
    {
        // Turning the knob flares the glow (light trail); hovering warms it.
        const float glowNow = juce::jlimit (0.0f, 2.0f,
                                            theme.glow * (1.0f + 1.6f * trail
                                                          + (hover ? 0.35f : 0.0f)));

        juce::Path progress;
        progress.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                                rotaryStartAngle, angle, true);

        // Full-glow themes sweep the arc from the accent toward the theme's
        // second neon (waveform colour): cyan->pink on Neon Ocean, pink->cyan
        // on Neon Rider, green->orange on Neo-Seoul.
        const juce::Colour arcEnd = theme.glow >= 0.9f
            ? theme.accent.interpolatedWith (theme.waveform, 0.85f)
            : theme.accent;
        juce::ColourGradient arcGrad (theme.accent,
                                      bounds.getX(), bounds.getBottom(),
                                      arcEnd,
                                      bounds.getRight(), bounds.getY(), false);

        // Layered outer halo (widest & faintest first) so the arc "emits" light.
        if (glowNow > 0.0f && ! mini)
        {
            for (int layer = 5; layer >= 1; --layer)
            {
                const float alpha = juce::jlimit (0.0f, 1.0f,
                                                  0.11f * glowNow * (float) layer);
                juce::ColourGradient halo (arcGrad);
                halo.multiplyOpacity (alpha);
                g.setGradientFill (halo);
                g.strokePath (progress,
                              juce::PathStrokeType (ringThickness + 4.2f * (float) layer,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
            }
        }

        // Crisp bright core on top of the halo.
        g.setGradientFill (arcGrad);
        g.strokePath (progress, juce::PathStrokeType (ringThickness,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        // Glowing tip dot at the value position (synthwave signature).
        if (theme.glow >= 0.5f && ! mini)
        {
            const juce::Point<float> tip (
                centre.x + ringRadius * std::cos (angle - juce::MathConstants<float>::halfPi),
                centre.y + ringRadius * std::sin (angle - juce::MathConstants<float>::halfPi));

            const float dotR = ringThickness * (0.9f + 0.6f * trail);
            g.setColour (theme.accent.withAlpha (juce::jlimit (0.0f, 1.0f,
                                                               0.40f * glowNow)));
            g.fillEllipse (juce::Rectangle<float> (dotR * 5.2f, dotR * 5.2f).withCentre (tip));
            g.setColour (theme.accent.withAlpha (juce::jlimit (0.0f, 1.0f,
                                                               0.75f * glowNow)));
            g.fillEllipse (juce::Rectangle<float> (dotR * 2.6f, dotR * 2.6f).withCentre (tip));
            g.setColour (juce::Colours::white.interpolatedWith (theme.accent, 0.15f));
            g.fillEllipse (juce::Rectangle<float> (dotR * 1.6f, dotR * 1.6f).withCentre (tip));
        }
    }

    //--------------------------------------------------------------------------
    // Value readout inside the disc, only while the user is looking at it —
    // a permanent "0.000" under every knob was pure noise.
    if (hover || trail > 0.03f)
    {
        g.setColour (theme.text.withAlpha (juce::jmin (1.0f, 0.55f + trail)));
        g.setFont (juce::Font (juce::FontOptions (
            juce::jlimit (9.0f, 13.0f, discRadius * 0.55f)).withStyle ("Medium")));
        g.drawText (slider.getTextFromValue (slider.getValue()),
                    discBounds, juce::Justification::centred);
    }

    //--------------------------------------------------------------------------
    // (e) Small crisp indicator: a short rounded line from mid-disc to the rim.
    {
        // Short tick near the rim only - the value readout lives mid-disc now.
        const float indicatorOuter = discRadius - 3.0f;
        const float indicatorInner = discRadius * 0.68f;
        const float thickness = juce::jmax (2.0f, radius * 0.06f);

        juce::Point<float> p1 (centre.x + indicatorInner * std::cos (angle - juce::MathConstants<float>::halfPi),
                               centre.y + indicatorInner * std::sin (angle - juce::MathConstants<float>::halfPi));
        juce::Point<float> p2 (centre.x + indicatorOuter * std::cos (angle - juce::MathConstants<float>::halfPi),
                               centre.y + indicatorOuter * std::sin (angle - juce::MathConstants<float>::halfPi));

        juce::Path indicator;
        indicator.startNewSubPath (p1);
        indicator.lineTo (p2);
        g.setColour (theme.dark ? juce::Colours::white.withAlpha (0.92f)
                                : juce::Colour (0xff1c1c1e));
        g.strokePath (indicator, juce::PathStrokeType (thickness,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    }
}

//==============================================================================
KnobComponent::KnobComponent (const juce::String& caption)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

    // ~270 degree sweep in the Apple style.
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f,
                                true);

    // No permanent text row: the dial takes the whole cell, and the value
    // appears INSIDE the disc while hovering or dragging.
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setLookAndFeel (&lookAndFeel);
    slider.setRepaintsOnMouseActivity (true);   // hover glow
    addAndMakeVisible (slider);

    label.setText (caption, juce::dontSendNotification);
    label.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Medium")));
    label.setJustificationType (juce::Justification::centred);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    slider.addListener (this);
}

void KnobComponent::sliderValueChanged (juce::Slider*)
{
    dragGlow = 1.0f;
    slider.getProperties().set ("dragGlow", dragGlow);
    if (! isTimerRunning())
        startTimerHz (30);
    slider.repaint();
}

void KnobComponent::timerCallback()
{
    dragGlow = juce::jmax (0.0f, dragGlow - 0.07f);
    slider.getProperties().set ("dragGlow", dragGlow);
    slider.repaint();
    if (dragGlow <= 0.0f)
        stopTimer();
}

KnobComponent::~KnobComponent()
{
    stopTimer();
    slider.removeListener (this);
    slider.setLookAndFeel (nullptr);
}

void KnobComponent::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromBottom (14));
    slider.setBounds (area);
}

void KnobComponent::paint (juce::Graphics& /*g*/)
{
    const auto& theme = ThemeManager::active();

    // Caption: secondary colour, medium weight (font set in constructor).
    label.setColour (juce::Label::textColourId, theme.textSecondary);

    // Minimal value readout under the dial: transparent background/outline.
    slider.setColour (juce::Slider::textBoxTextColourId, theme.text);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxHighlightColourId, theme.accentSoft);
}
