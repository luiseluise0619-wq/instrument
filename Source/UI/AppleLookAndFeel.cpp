#include "AppleLookAndFeel.h"
#include "ThemeManager.h"

#include <cmath>

AppleLookAndFeel::AppleLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
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

    const float radius = juce::jmin (theme.cornerRadius, bounds.getHeight() * 0.5f);

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
        g.fillRect (bounds.getX() + radius, bounds.getY() + 1.0f,
                    bounds.getWidth() - radius * 2.0f, 1.0f);
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

    auto bounds = button.getLocalBounds().toFloat();
    const float boxSide = juce::jlimit (13.0f, 18.0f, bounds.getHeight() - 6.0f);
    auto box = juce::Rectangle<float> (boxSide, boxSide)
                   .withCentre ({ bounds.getX() + boxSide * 0.5f + 1.0f,
                                  bounds.getCentreY() });
    const float r = boxSide * 0.30f;

    // Filled accent box when on, hairline well when off - the stock JUCE tick
    // box was invisible on the light themes and drab on the dark ones.
    if (on)
    {
        if (theme.glow >= 0.5f)
        {
            g.setColour (theme.accent.withAlpha (0.30f * theme.glow));
            g.fillRoundedRectangle (box.expanded (3.0f), r + 3.0f);
        }
        g.setColour (theme.accent.brighter (highlighted ? 0.15f : 0.0f));
        g.fillRoundedRectangle (box, r);

        // Tick.
        juce::Path tick;
        tick.startNewSubPath (box.getX() + boxSide * 0.24f, box.getY() + boxSide * 0.52f);
        tick.lineTo          (box.getX() + boxSide * 0.43f, box.getY() + boxSide * 0.71f);
        tick.lineTo          (box.getX() + boxSide * 0.77f, box.getY() + boxSide * 0.30f);
        g.setColour (theme.dark ? juce::Colours::white
                                : juce::Colours::white.withAlpha (0.97f));
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour (theme.control.withAlpha (theme.dark ? 1.0f : 0.85f));
        g.fillRoundedRectangle (box, r);
        g.setColour (highlighted ? theme.accent.withAlpha (0.75f)
                                 : theme.textSecondary.withAlpha (0.55f));
        g.drawRoundedRectangle (box.reduced (0.5f), r, 1.2f);
    }

    // Label: full-strength text, never the washed-out default.
    g.setColour (theme.text.withAlpha (button.isEnabled() ? (down ? 0.75f : 1.0f) : 0.4f));
    g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Medium")));
    g.drawText (button.getButtonText(),
                bounds.withTrimmedLeft (boxSide + 9.0f),
                juce::Justification::centredLeft, false);
}

void AppleLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height,
                                     bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& theme = ThemeManager::active();
    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    const float radius = juce::jmin (theme.cornerRadius, bounds.getHeight() * 0.5f);

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
                    bounds.getWidth() - radius * 2.0f, 1.0f);
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
        g.drawRoundedRectangle (bounds.reduced (1.5f), radius - 1.5f, 2.0f);
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

    const float radius = 12.0f;

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

        if (glowTheme)
        {
            // Neon treatment: translucent accent wash + thin glowing left bar.
            g.setColour (theme.accentSoft);
            g.fillRoundedRectangle (rf, 7.0f);

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
            g.fillRoundedRectangle (rf, 7.0f);
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
