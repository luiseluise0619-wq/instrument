#include "AppleLookAndFeel.h"
#include "ThemeManager.h"

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

    juce::Colour fill = theme.materialStrong;
    if (button.getToggleState())          fill = theme.accent;
    else if (down)                        fill = theme.accentSoft.withMultipliedAlpha (2.0f);
    else if (highlighted)                 fill = theme.material.brighter (0.06f);

    // Soft shadow.
    juce::DropShadow (theme.shadow, 8, { 0, 2 })
        .drawForRectangle (g, bounds.getSmallestIntegerContainer());

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

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);

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
    if (button.getToggleState())
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

    g.setColour (theme.materialStrong);
    g.fillRoundedRectangle (bounds, radius);

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

    g.setColour (theme.dark ? juce::Colour (0xf01e1e20) : juce::Colour (0xf7ffffff));
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (theme.separator);
    g.drawRoundedRectangle (bounds, 10.0f, 1.0f);
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

    g.setColour (isHighlighted && isActive ? (glowTheme ? theme.text : juce::Colours::white)
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
