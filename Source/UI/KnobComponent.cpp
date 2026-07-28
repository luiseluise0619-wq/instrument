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

    // Disc inset far enough that the tick ring and the value arc both have
    // room outside it. The spec's proportions: body to ~84% of the radius,
    // arc just outside that, ticks in the outermost band.
    const float discRadius = radius * 0.78f;
    auto discBounds = juce::Rectangle<float> (discRadius * 2.0f, discRadius * 2.0f).withCentre (centre);

    const bool  hover = slider.isMouseOverOrDragging();
    const bool  learn = (bool) slider.getProperties().getWithDefault ("learnGlow", false);
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
        // The face is a two-stop gradient between two THEME tokens rather
        // than one colour lightened and darkened. That is what lets each
        // theme's knob feel like a different material instead of the same
        // grey dial recoloured - the tokens are already accent-tinted.
        const auto top = theme.glow >= 0.9f
                            ? theme.materialStrong.withAlpha (1.0f).brighter (0.25f)
                            : theme.control;
        const auto bot = theme.glow >= 0.9f
                            ? theme.materialStrong.withAlpha (1.0f)
                            : theme.controlBottom;

        // 160 degrees, so the light reads as coming from the upper left
        // rather than straight down - a vertical gradient on a circle looks
        // printed, an angled one looks turned.
        const float a = juce::degreesToRadians (160.0f);
        const float dx = std::sin (a) * discRadius, dy = -std::cos (a) * discRadius;
        juce::ColourGradient face (top, centre.x - dx, centre.y - dy,
                                   bot, centre.x + dx, centre.y + dy, false);
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

    // Learn mark: hovering a macro points out the knobs it moves.
    //
    // This was a 0.55-alpha accent bloom behind the dial, and at that strength
    // four knobs lighting up at once did not read as "these are linked", it
    // read as the panel glitching - which is how it got reported. The job is
    // to answer "what does this macro touch", and a thin ring on the rim
    // answers it without turning a quarter of the window blue. If a hint needs
    // to shout to be noticed, it is in the wrong place, not too quiet.
    if (learn)
    {
        g.setColour (theme.accent.withAlpha (0.85f));
        g.drawEllipse (discBounds.expanded (radius * 0.10f), 1.6f);
    }

    //--------------------------------------------------------------------------
    // (b2) Tick ring - 24 marks across the 270 degree sweep, one every 11.25
    // degrees. This is the element that makes a dial read as an instrument
    // rather than as a progress ring, and it was the single biggest reason
    // twenty-five of these in a window looked generic. Static on purpose: the
    // arc shows the value, the ticks show the RANGE.
    if (! mini)
    {
        constexpr int kTicks = 24;
        const float tickOuter = radius * 1.00f;
        const float tickInner = radius * 0.93f;
        juce::Path ticks;
        for (int i = 0; i <= kTicks; ++i)
        {
            const float t  = (float) i / (float) kTicks;
            const float th = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle)
                           - juce::MathConstants<float>::halfPi;
            const float c = std::cos (th), sn = std::sin (th);
            ticks.startNewSubPath (centre.x + tickInner * c, centre.y + tickInner * sn);
            ticks.lineTo         (centre.x + tickOuter * c, centre.y + tickOuter * sn);
        }
        g.setColour (theme.glow >= 0.9f ? theme.accent.withAlpha (0.30f) : theme.tick);
        g.strokePath (ticks, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::butt));
    }

    //--------------------------------------------------------------------------
    // Ring geometry (between the disc and the tick ring).
    const float ringRadius = mini ? radius - 2.0f : radius * 0.845f;
    const float ringThickness = juce::jmax (2.5f, radius * (mini ? 0.10f : 0.085f));

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
    //
    // The macros opt out of that (setAlwaysShowValue): they are the three dials
    // whose position is the headline, and they show a whole-number percentage
    // rather than the raw parameter text, because "0.470" is not a thing anyone
    // says about a macro.
    const bool always = (bool) slider.getProperties()
                                     .getWithDefault ("alwaysShowValue", false);

    if (always || hover || trail > 0.03f)
    {
        const float alpha = always ? juce::jmin (1.0f, 0.80f + trail)
                                   : juce::jmin (1.0f, 0.55f + trail);
        g.setColour (theme.text.withAlpha (alpha));
        g.setFont (juce::Font (juce::FontOptions (
            juce::jlimit (9.0f, 13.0f, discRadius * 0.55f)).withStyle ("Medium")));

        const auto readout = always
            ? juce::String (juce::roundToInt (
                  juce::jlimit (0.0, 1.0,
                                slider.valueToProportionOfLength (slider.getValue()))
                  * 100.0)) + "%"
            : slider.getTextFromValue (slider.getValue());

        g.drawText (readout, discBounds, juce::Justification::centred);
    }

    //--------------------------------------------------------------------------
    // (e) Small crisp indicator: a short rounded line from mid-disc to the rim.
    {
        // Short tick near the rim only - the value readout lives mid-disc now.
        // Runs from near the centre out to the rim, per the spec's 14% inset.
        // The short version sat entirely in the outer third of the face and
        // read as a speck rather than as a pointer.
        const float indicatorOuter = discRadius - 2.0f;
        const float indicatorInner = discRadius * 0.16f;
        const float thickness = juce::jmax (2.0f, radius * 0.055f);

        juce::Point<float> p1 (centre.x + indicatorInner * std::cos (angle - juce::MathConstants<float>::halfPi),
                               centre.y + indicatorInner * std::sin (angle - juce::MathConstants<float>::halfPi));
        juce::Point<float> p2 (centre.x + indicatorOuter * std::cos (angle - juce::MathConstants<float>::halfPi),
                               centre.y + indicatorOuter * std::sin (angle - juce::MathConstants<float>::halfPi));

        juce::Path indicator;
        indicator.startNewSubPath (p1);
        indicator.lineTo (p2);
        // Fades from nearly invisible at the centre to the accent at the rim,
        // so the pointer reads as the same signal the arc is showing rather
        // than as a separate white mark competing with it.
        const auto tipCol = theme.glow >= 0.9f
                              ? juce::Colours::white.interpolatedWith (theme.accent, 0.25f)
                              : theme.accent;
        juce::ColourGradient ind (tipCol.withAlpha (0.0f), p1,
                                  tipCol,                  p2, false);
        ind.addColour (0.45, tipCol.withAlpha (0.30f));
        ind.addColour (0.80, tipCol.withAlpha (0.92f));
        g.setGradientFill (ind);
        g.strokePath (indicator, juce::PathStrokeType (thickness,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    }
}

//==============================================================================
KnobComponent::KnobComponent (const juce::String& captionText)
    : caption (captionText)
{
    // Vertical drag only (spec section 3). The combined horizontal+vertical
    // style adds the two axes together, so a drag that wanders sideways while
    // going up moves the value further than the pointer did - which is exactly
    // the "the knob ran away from me" complaint. One axis, one meaning.
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);

    // 180px of travel = full range, and an up/down cursor to say so before the
    // user commits to a drag. (JUCE's default is 250px.)
    slider.setMouseDragSensitivity (180);
    slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);

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

    subLabel.setJustificationType (juce::Justification::centred);
    subLabel.setInterceptsMouseClicks (false, false);
    subLabel.setFont (juce::Font (juce::FontOptions (9.5f)));
    addChildComponent (subLabel);          // shown only once one is set

    slider.addListener (this);
    slider.addMouseListener (&hoverWatcher, true);

    // "Label - NN%". Written now so a knob is self-describing even before an
    // attachment moves it; owners that want to say more overwrite it (see
    // updateTooltip) and keep it.
    updateTooltip();
}

//==============================================================================
void KnobComponent::updateTooltip()
{
    // The rule is "never clobber a tooltip the owner set", and the only way to
    // know is to remember what we wrote last. Anything else in there came from
    // outside - arpGateKnob and pumpKnob both carry a sentence of their own -
    // so back off permanently and let it stand.
    const auto existing = slider.getTooltip();
    if (existing.isNotEmpty() && existing != ownTooltip)
        return;

    const int pct = juce::roundToInt (
        juce::jlimit (0.0, 1.0, slider.valueToProportionOfLength (slider.getValue()))
            * 100.0);

    // U+2014 EM DASH, spelled out so this file stays pure ASCII on disk.
    ownTooltip = caption + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 "))
                         + juce::String (pct) + "%";
    slider.setTooltip (ownTooltip);
}

void KnobComponent::setAlwaysShowValue (bool shouldAlwaysShow)
{
    slider.getProperties().set ("alwaysShowValue", shouldAlwaysShow);
    slider.repaint();
}

void KnobComponent::setLearnGlow (bool on)
{
    if (learnGlow == on) return;
    learnGlow = on;
    slider.getProperties().set ("learnGlow", on);
    slider.repaint();
}

void KnobComponent::setSubCaption (const juce::String& text)
{
    subLabel.setText (text, juce::dontSendNotification);
    subLabel.setVisible (text.isNotEmpty());
    resized();
}

void KnobComponent::sliderValueChanged (juce::Slider*)
{
    dragGlow = 1.0f;
    slider.getProperties().set ("dragGlow", dragGlow);
    if (! isTimerRunning())
        startTimerHz (30);
    updateTooltip();
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
    slider.removeMouseListener (&hoverWatcher);
    slider.removeListener (this);
    slider.setLookAndFeel (nullptr);
}

void KnobComponent::resized()
{
    auto area = getLocalBounds();
    if (subLabel.isVisible())
        subLabel.setBounds (area.removeFromBottom (12));
    label.setBounds (area.removeFromBottom (14));
    slider.setBounds (area);
}

void KnobComponent::paint (juce::Graphics& /*g*/)
{
    const auto& theme = ThemeManager::active();

    // Caption: secondary colour, medium weight (font set in constructor).
    label.setColour (juce::Label::textColourId, theme.textSecondary);
    // The sub-caption names what a macro moves, so it is the one line on the
    // knob that carries information the dial itself cannot show. At 0.55 alpha
    // on top of an already-dimmed colour it measured barely above the card it
    // sat on - present in a screenshot, invisible in use.
    subLabel.setColour (juce::Label::textColourId, theme.text.withAlpha (0.68f));

    // Minimal value readout under the dial: transparent background/outline.
    slider.setColour (juce::Slider::textBoxTextColourId, theme.text);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxHighlightColourId, theme.accentSoft);
}
