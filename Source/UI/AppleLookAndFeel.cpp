#include "AppleLookAndFeel.h"
#include "ThemeManager.h"

#include <cmath>

namespace
{
    /** cubic-bezier(.32,.72,0,1) - the curve macOS uses for its switches.
        It leaves fast and settles slowly, which is what makes the thumb feel
        thrown rather than dragged.

        There is no closed form for y(x) on a cubic bezier, so solve x for t by
        bisection. Sixteen halvings put t within 1/65536, far below the ~16px
        of travel this drives, and it is a handful of multiplies once per frame
        for at most a couple of switches. */
    float switchEase (float x) noexcept
    {
        constexpr float x1 = 0.32f, y1 = 0.72f;
        constexpr float x2 = 0.0f,  y2 = 1.0f;

        auto bezier = [] (float a, float b, float t)
        {
            const float u = 1.0f - t;
            return 3.0f * u * u * t * a + 3.0f * u * t * t * b + t * t * t;
        };

        x = juce::jlimit (0.0f, 1.0f, x);

        float lo = 0.0f, hi = 1.0f, t = x;
        for (int i = 0; i < 16; ++i)
        {
            t = 0.5f * (lo + hi);
            if (bezier (x1, x2, t) < x) lo = t;
            else                        hi = t;
        }

        return bezier (y1, y2, t);
    }
}

AppleLookAndFeel::AppleLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
}

void AppleLookAndFeel::drawComboBoxTextWhenNothingSelected (juce::Graphics& g,
                                                            juce::ComboBox& box,
                                                            juce::Label& label)
{
    // The placeholder ("Sound 1", "Instrument", ...) is drawn HERE, not by the
    // label, and JUCE's version reads the colour with a bare findColour() -
    // which resolves against the look-and-feel's own scheme, never against the
    // box. So every setColour (ComboBox::textColourId, ...) we make was being
    // thrown away for this one piece of text.
    //
    // In the dark themes that was invisible as a bug, because the stock colour
    // is near-white and near-white on a dark card happens to be correct. It
    // only showed up in the light themes, as white text on a white field - the
    // looper's six track pickers looked completely empty.
    g.setColour (box.findColour (juce::ComboBox::textColourId).withMultipliedAlpha (0.55f));
    g.setFont (label.getLookAndFeel().getLabelFont (label));

    auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
    g.drawFittedText (box.getTextWhenNothingSelected(), area,
                      label.getJustificationType(), 1,
                      label.getMinimumHorizontalScale());
}

juce::PopupMenu::Options
AppleLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    auto opts = LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label);
    const int cols = (int) box.getProperties().getWithDefault ("menuColumns", 1);
    if (cols > 1)
        opts = opts.withMaximumNumColumns (cols);
    return opts;
}

float AppleLookAndFeel::radiusFor (juce::Component& c, juce::Rectangle<float> bounds,
                                   float defaultRadius)
{
    const auto& props = c.getProperties();

    float r = defaultRadius;
    if      ((bool) props.getWithDefault ("pill",         false)) r = AppleRadius::pill;
    else if ((bool) props.getWithDefault ("chip",         false)) r = AppleRadius::chip;
    else if ((bool) props.getWithDefault ("largeControl", false)) r = AppleRadius::large;
    else if ((bool) props.getWithDefault ("panel",        false)) r = AppleRadius::panel;

    return juce::jmin (r, bounds.getHeight() * 0.5f);
}

//==============================================================================
juce::Font AppleLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions ((float) juce::jmin (15, buttonHeight - 8))
                           .withStyle ("Semibold"));
}

void AppleLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                             const juce::Colour&,
                                             bool highlighted, bool down)
{
    const auto& theme = ThemeManager::active();
    const bool  glowTheme = theme.glow >= 0.9f;

    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    if (glowTheme)
        bounds = bounds.reduced (2.5f);   // margin inside the clip for the under-glow

    // Buttons sit at 10 on the radius scale; a button flagged "pill" (or
    // "chip") picks up its own curve from the same scale.
    const float radius = radiusFor (button, bounds, AppleRadius::button);

    // A button marked "primaryAction" is filled with the accent, the way the
    // confirming button in a macOS sheet is.
    const bool primary = (bool) button.getProperties().getWithDefault ("primaryAction", false);

    juce::Colour fill = theme.materialStrong;
    if (button.getToggleState())          fill = theme.accent;
    else if (primary)                     fill = down        ? theme.accent.darker (0.18f)
                                               : highlighted ? theme.accent.brighter (0.08f)
                                                             : theme.accent;
    else if (down)                        fill = theme.accentSoft.withMultipliedAlpha (2.0f);
    else if (highlighted)                 fill = theme.material.brighter (0.06f);

    // Soft shadow - two offset fills. A gaussian DropShadow per button per
    // paint is exactly what made the panel feel sluggish before.
    g.setColour (theme.shadow.withAlpha (0.20f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f), radius);
    g.setColour (theme.shadow.withAlpha (0.12f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), radius);

    // Optional click-flash pulse (0..1) that owners drive via the "neonFlash"
    // component property (see ChordBar). Glow themes only.
    const float flash = glowTheme
        ? juce::jlimit (0.0f, 1.0f,
                        (float) button.getProperties().getWithDefault ("neonFlash", 0.0f))
        : 0.0f;

    // On glow themes, buttons sit on a neon under-glow: soft on hover,
    // strong when pressed / toggled, flaring cyan -> purple with the flash.
    if (glowTheme)
    {
        float glowAmt = highlighted ? 0.45f : 0.0f;
        if (down || button.getToggleState())
            glowAmt = 1.0f;
        glowAmt = juce::jmax (glowAmt, flash);

        if (glowAmt > 0.0f)
        {
            const juce::Colour glowCol =
                theme.accent.interpolatedWith (juce::Colour (0xffb026ff), 0.6f * flash);

            juce::Path halo;
            halo.addRoundedRectangle (bounds, radius);
            juce::DropShadow (glowCol.withAlpha (juce::jlimit (0.0f, 1.0f, 0.55f * glowAmt)),
                              7, { 0, 1 }).drawForPath (g, halo);

            g.setColour (glowCol.withAlpha (0.30f * glowAmt));
            g.drawRoundedRectangle (bounds.expanded (1.0f), radius + 1.0f, 1.5f);
        }
    }

    // Face: a top-lit vertical gradient reads as a physical key rather than
    // a painted rectangle. Toggled (accent-filled) buttons keep it too.
    {
        const float lift = theme.dark ? 0.16f : 0.10f;
        juce::ColourGradient face (fill.brighter (down ? 0.0f : lift),
                                   bounds.getX(), bounds.getY(),
                                   fill.darker (down ? 0.06f : 0.05f),
                                   bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (face);
        g.fillRoundedRectangle (bounds, radius);
    }

    // 1px inner highlight along the top edge - the "glass" line.
    if (! down)
    {
        g.setColour (juce::Colours::white.withAlpha (theme.dark ? 0.07f : 0.55f));
        // jmax: a pill's radius is half its height, which on a tall narrow
        // button is wider than the button - a negative width here draws junk.
        g.fillRect (bounds.getX() + radius, bounds.getY() + 1.0f,
                    juce::jmax (0.0f, bounds.getWidth() - radius * 2.0f), 1.0f);
    }

    // The click flash also brightens the face with an accent wash.
    if (glowTheme && flash > 0.0f && ! button.getToggleState())
    {
        g.setColour (theme.accentSoft.withMultipliedAlpha (
            juce::jlimit (0.0f, 1.0f, 1.4f * flash)));
        g.fillRoundedRectangle (bounds, radius);
    }

    // Hovered buttons pick up a faint accent wash on glow themes.
    if (glowTheme && highlighted && ! down && ! button.getToggleState())
    {
        g.setColour (theme.accentSoft.withMultipliedAlpha (0.7f));
        g.fillRoundedRectangle (bounds, radius);
    }

    // Buttons must read as buttons even at rest: glow themes always get an
    // accent rim (brighter when active), other themes a clear hairline.
    if (glowTheme)
        g.setColour (theme.accent.withAlpha (
            (down || highlighted || button.getToggleState() || flash > 0.0f) ? 0.75f : 0.40f));
    else
        g.setColour (theme.separator);
    g.drawRoundedRectangle (bounds, radius, glowTheme ? 1.2f : 1.0f);
}

void AppleLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                       bool, bool)
{
    const auto& theme = ThemeManager::active();
    g.setFont (getTextButtonFont (button, button.getHeight()));

    // Toggled buttons are filled with the accent — on the neon-cyan theme
    // white text vanishes into it, so use deep navy there instead.
    const bool primary = (bool) button.getProperties().getWithDefault ("primaryAction", false);
    if (button.getToggleState() || primary)
        g.setColour (theme.glow >= 0.9f ? juce::Colour (0xff041022)
                                        : juce::Colours::white);
    else
        g.setColour (theme.text);

    g.drawText (button.getButtonText(), button.getLocalBounds(),
                juce::Justification::centred, true);
}

//==============================================================================
juce::Font AppleLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (14.0f).withStyle ("Medium"));
}

void AppleLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                         bool highlighted, bool down)
{
    const auto& theme = ThemeManager::active();
    const bool  on    = button.getToggleState();

    //--------------------------------------------------------------------------
    // A macOS switch, not a tick box (spec section 4.5). The difference is not
    // decoration: a tick box says "is this option selected", a switch says
    // "is this thing running right now", and Reverse / Ping-Pong are the
    // second kind. The travelling thumb is the whole point - it is what makes
    // the state readable from the corner of your eye while you are playing.
    //
    // Animation state has to live somewhere, and LookAndFeel is shared by every
    // button, so it goes in the BUTTON's own property set. Each paint reads the
    // clock, advances the phase and asks for another repaint until it lands -
    // no timer to own, nothing to unregister, and a switch that is never on
    // screen costs nothing.
    auto& props = button.getProperties();

    const double target = on ? 1.0 : 0.0;
    const double now    = juce::Time::getMillisecondCounterHiRes();

    double phase = (double) props.getWithDefault ("switchPhase", target);
    double from  = (double) props.getWithDefault ("switchFrom",  target);
    double t0    = (double) props.getWithDefault ("switchT0",    now);

    // First paint has no stored target, and must SNAP: a switch that animates
    // itself on every theme change or window open reads as a glitch.
    const bool known  = props.contains ("switchTarget");
    const bool lastOn = ((double) props.getWithDefault ("switchTarget", target)) > 0.5;

    if (! known || lastOn != on)
    {
        from = known ? phase : target;
        t0   = now;
        props.set ("switchFrom",   from);
        props.set ("switchT0",     t0);
        props.set ("switchTarget", target);
    }

    constexpr double kDurationMs = 180.0;
    const double u = juce::jlimit (0.0, 1.0, (now - t0) / kDurationMs);

    phase = juce::jlimit (0.0, 1.0, from + (target - from) * (double) switchEase ((float) u));
    props.set ("switchPhase", phase);

    const float p = (float) phase;

    //--------------------------------------------------------------------------
    // Geometry: 38 x 22 track, radius 11, a 16px thumb inset 3px, so the thumb
    // travels 3 -> 19. Scaled down proportionally when the row is shorter than
    // 22px (the playback card sizes its rows from the card height).
    auto bounds = button.getLocalBounds().toFloat();

    const float scale = juce::jlimit (0.45f, 1.0f,
                                      juce::jmin (bounds.getHeight() / 22.0f,
                                                  bounds.getWidth()  / 38.0f));
    const float trackW = 38.0f * scale;
    const float trackH = 22.0f * scale;
    const float trackR = 11.0f * scale;
    const float thumbD = 16.0f * scale;
    const float inset  =  3.0f * scale;
    const float travel = trackW - thumbD - inset * 2.0f;   // 16 * scale

    auto track = juce::Rectangle<float> (trackW, trackH)
                     .withCentre ({ bounds.getX() + trackW * 0.5f + 1.0f,
                                    bounds.getCentreY() });

    auto thumb = juce::Rectangle<float> (thumbD, thumbD)
                     .withCentre ({ track.getX() + inset + thumbD * 0.5f + travel * p,
                                    track.getCentreY() });

    //--------------------------------------------------------------------------
    // Under-glow on the neon themes, so a live switch reads as lit.
    if (theme.glow >= 0.5f && p > 0.01f)
    {
        g.setColour (theme.accent.withAlpha (juce::jlimit (0.0f, 1.0f,
                                                           0.28f * p * theme.glow)));
        g.fillRoundedRectangle (track.expanded (3.0f * scale), trackR + 3.0f * scale);
    }

    // Track: fills from the inactive-track token to the accent as it turns on.
    // controlTrack is a translucent overlay token (18% white on dark, 8% black
    // on light) - at face value it leaves a white thumb sitting on almost
    // nothing, so lift the alpha before blending.
    {
        const auto off = theme.controlTrack.withMultipliedAlpha (theme.dark ? 1.5f : 2.6f);
        auto fill = off.interpolatedWith (theme.accent, p);
        if (highlighted)
            fill = fill.brighter (0.10f);

        g.setColour (fill);
        g.fillRoundedRectangle (track, trackR);

        // Hairline while off (the light themes need the edge), gone once the
        // accent is carrying the shape itself.
        g.setColour (theme.separator.withMultipliedAlpha (1.0f - p));
        g.drawRoundedRectangle (track.reduced (0.5f), trackR - 0.5f, 1.0f);
    }

    // Soft shadow under the thumb. Two offset fills rather than a gaussian:
    // this runs every frame of the animation.
    {
        g.setColour (theme.shadow.withMultipliedAlpha (0.55f));
        g.fillEllipse (thumb.translated (0.0f, 1.6f * scale).expanded (0.5f * scale));
        g.setColour (theme.shadow.withMultipliedAlpha (0.35f));
        g.fillEllipse (thumb.translated (0.0f, 0.7f * scale));
    }

    // The thumb is the one thing the spec fixes as pure white, on every theme.
    g.setColour (juce::Colours::white);
    g.fillEllipse (thumb);
    g.setColour (theme.shadow.withMultipliedAlpha (0.22f));
    g.drawEllipse (thumb.reduced (0.5f), 1.0f);

    //--------------------------------------------------------------------------
    // Label to the right of the switch, in primary text.
    g.setColour (theme.text.withAlpha (button.isEnabled() ? (down ? 0.75f : 1.0f) : 0.4f));
    g.setFont (juce::Font (juce::FontOptions (juce::jlimit (10.5f, 13.0f, 13.0f * scale))
                               .withStyle ("Medium")));
    g.drawText (button.getButtonText(),
                bounds.withTrimmedLeft (track.getRight() - bounds.getX() + 9.0f * scale),
                juce::Justification::centredLeft, false);

    // Keep the animation running until it arrives, then stop asking. The
    // second test matters on the first paint and on a theme change, where
    // there is nothing to travel and a repaint loop would just burn frames.
    if (u < 1.0 && std::abs (target - from) > 1.0e-4)
        button.repaint();
}

void AppleLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height,
                                     bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& theme = ThemeManager::active();
    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    // Fields share the buttons' 10 on the radius scale - they are the same
    // size of object and belong to the same row of the toolbar.
    const float radius = radiusFor (box, bounds, AppleRadius::button);

    const bool glowTheme = theme.glow >= 0.9f;
    const bool focused   = box.hasKeyboardFocus (false);

    // Hover lights the rim on glow themes; make sure the box repaints on
    // mouse activity so the rim tracks the cursor.
    if (glowTheme)
        box.setRepaintsOnMouseActivity (true);
    const bool hovered = glowTheme && box.isMouseOver (true);

    // Recessed well: darker at the top (light falls in), 1px light lip at the
    // bottom - the inverse of the raised buttons, so the two read differently.
    {
        const auto base = theme.materialStrong;
        juce::ColourGradient well (base.darker (theme.dark ? 0.14f : 0.03f),
                                   bounds.getX(), bounds.getY(),
                                   base.brighter (theme.dark ? 0.06f : 0.0f),
                                   bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill (well);
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (juce::Colours::white.withAlpha (theme.dark ? 0.05f : 0.45f));
        g.fillRect (bounds.getX() + radius, bounds.getBottom() - 1.5f,
                    juce::jmax (0.0f, bounds.getWidth() - radius * 2.0f), 1.0f);
    }

    // Combos always show an accent rim on glow themes so they can't sink
    // into the dark cards; brighter when hovered/focused.
    if (glowTheme)
        g.setColour (theme.accent.withAlpha (focused || hovered ? 0.9f : 0.40f));
    else
        g.setColour (focused || hovered ? theme.accent : theme.separator);
    g.drawRoundedRectangle (bounds, radius, glowTheme ? 1.2f : 1.0f);

    // Faint accent halo just inside the lit rim on glow themes.
    if ((focused || hovered) && glowTheme)
    {
        g.setColour (theme.accent.withAlpha (0.18f));
        g.drawRoundedRectangle (bounds.reduced (1.5f), juce::jmax (0.0f, radius - 1.5f), 2.0f);
        g.setColour (theme.accent.withAlpha (0.10f));
        g.drawRoundedRectangle (bounds.expanded (1.0f), radius + 1.0f, 1.5f);
    }

    // Chevron (two short strokes forming a downward "v").
    const float cx = (float) width - 16.0f;
    const float cy = (float) height * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (theme.textSecondary);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void AppleLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (12, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId, ThemeManager::active().text);
}

//==============================================================================
void AppleLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto& theme = ThemeManager::active();
    auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (1.0f);

    // Floating menus are large controls on the radius scale (spec: the theme
    // menu is 14), not buttons.
    const float radius = AppleRadius::large;

    // Soft stacked shadow so the menu floats over the panel.
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.10f));
        g.fillRoundedRectangle (bounds.translated (0.0f, (float) i * 1.5f)
                                      .expanded ((float) i * 0.8f),
                                radius + (float) i * 0.8f);
    }

    // Menu material carries a trace of the theme's ground, like macOS
    // vibrancy picking up the desktop behind it.
    const auto base = theme.dark ? theme.bgTop.brighter (0.22f).withAlpha (0.98f)
                                 : juce::Colour (0xfaffffff);
    g.setColour (base);
    g.fillRoundedRectangle (bounds, radius);

    juce::ColourGradient sheen (juce::Colours::white.withAlpha (theme.dark ? 0.05f : 0.5f),
                                bounds.getX(), bounds.getY(),
                                juce::Colours::white.withAlpha (0.0f),
                                bounds.getX(), bounds.getY() + 40.0f, false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (theme.separator);
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

void AppleLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g,
                                                   const juce::Rectangle<int>& area,
                                                   const juce::String& sectionName)
{
    const auto& theme = ThemeManager::active();

    // Small, letter-spaced, secondary - a Finder sidebar heading, not a menu
    // entry the user might try to click.
    g.setColour (theme.textSecondary.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Semibold"))
                   .withExtraKerningFactor (0.14f));
    g.drawText (sectionName.toUpperCase(),
                area.reduced (14, 0).withTrimmedTop (5),
                juce::Justification::centredLeft, false);

    g.setColour (theme.separator.withAlpha (0.55f));
    g.fillRect (area.getX() + 14, area.getBottom() - 2, area.getWidth() - 28, 1);
}

//==============================================================================
juce::Rectangle<int> AppleLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                         juce::Point<int> screenPos,
                                                         juce::Rectangle<int> parentArea)
{
    // Wrap long help text instead of stretching one endless line off-screen.
    const auto font = juce::Font (juce::FontOptions (12.5f));
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (tipText, font);

    juce::TextLayout layout;
    layout.createLayout (s, 320.0f);

    // Ceil, never truncate: drawTooltip re-lays the text out at
    // (width - 22), so a truncated measurement is narrower than what was
    // measured and the widest line gains a row that does not fit.
    const int w = (int) std::ceil (layout.getWidth())  + 22;
    const int h = (int) std::ceil (layout.getHeight()) + 16;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12)
                                                                      : screenPos.x + 18,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8)
                                                                      : screenPos.y + 18,
                                 w, h)
               .constrainedWithin (parentArea);
}

void AppleLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text,
                                    int width, int height)
{
    const auto& theme = ThemeManager::active();
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    const float radius = 8.0f;

    for (int i = 2; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRoundedRectangle (bounds.reduced (1.0f).translated (0.0f, (float) i),
                                radius);
    }

    g.setColour (theme.dark ? theme.bgTop.brighter (0.28f).withAlpha (0.97f)
                            : juce::Colour (0xfa1c1e24));
    g.fillRoundedRectangle (bounds.reduced (1.0f), radius);
    g.setColour (theme.dark ? theme.separator : juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (bounds.reduced (1.5f), radius, 1.0f);

    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, juce::Font (juce::FontOptions (12.5f)),
              theme.dark ? theme.text : juce::Colours::white);

    juce::TextLayout layout;
    layout.createLayout (s, (float) width - 22.0f);
    layout.draw (g, bounds.reduced (11.0f, 8.0f));
}

juce::Font AppleLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (14.0f).withStyle ("Medium"));
}

void AppleLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                          bool isSeparator, bool isActive, bool isHighlighted,
                                          bool isTicked, bool, const juce::String& text,
                                          const juce::String&, const juce::Drawable*,
                                          const juce::Colour*)
{
    const auto& theme = ThemeManager::active();

    if (isSeparator)
    {
        g.setColour (theme.separator);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    const bool glowTheme = theme.glow >= 0.9f;

    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        const auto rf = r.toFloat();
        // A highlighted row is a chip on the radius scale.
        const float rowR = juce::jmin (AppleRadius::chip, rf.getHeight() * 0.5f);

        if (glowTheme)
        {
            // Neon treatment: translucent accent wash + thin glowing left bar.
            g.setColour (theme.accentSoft);
            g.fillRoundedRectangle (rf, rowR);

            const juce::Rectangle<float> bar (rf.getX() + 1.5f, rf.getY() + 3.0f,
                                              2.5f, rf.getHeight() - 6.0f);
            g.setColour (theme.accent.withAlpha (0.30f));
            g.fillRoundedRectangle (bar.expanded (2.0f), 3.5f);
            g.setColour (theme.accent);
            g.fillRoundedRectangle (bar, 1.25f);
        }
        else
        {
            g.setColour (theme.accent);
            g.fillRoundedRectangle (rf, rowR);
        }
    }

    // The highlighted row is filled with the accent, so its text has to be the
    // theme's INK for that accent - flat white disappears on the light themes,
    // whose accents are pale.
    g.setColour (isHighlighted && isActive ? (glowTheme ? theme.text : theme.accentInk)
                                           : (isActive ? theme.text
                                                       : theme.textSecondary));
    g.setFont (getPopupMenuFont());

    auto textArea = r.reduced (10, 0);
    if (isTicked)
    {
        auto tick = textArea.removeFromLeft (18);
        juce::Path check;
        const float ty = (float) tick.getCentreY();
        const float tx = (float) tick.getX() + 2.0f;
        check.startNewSubPath (tx, ty);
        check.lineTo (tx + 4.0f, ty + 4.0f);
        check.lineTo (tx + 11.0f, ty - 5.0f);
        g.strokePath (check, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
    else
    {
        textArea.removeFromLeft (18);
    }

    g.drawText (text, textArea, juce::Justification::centredLeft, true);
}
