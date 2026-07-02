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
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = juce::jmin (theme.cornerRadius, bounds.getHeight() * 0.5f);

    juce::Colour fill = theme.materialStrong;
    if (button.getToggleState())          fill = theme.accent;
    else if (down)                        fill = theme.accentSoft.withMultipliedAlpha (2.0f);
    else if (highlighted)                 fill = theme.material.brighter (0.06f);

    // Soft shadow.
    juce::DropShadow (theme.shadow, 8, { 0, 2 }).drawForRectangle (g, button.getLocalBounds());

    // On glow themes, toggled / pressed buttons bloom softly outward.
    const bool glowTheme = theme.glow >= 0.9f;
    if (glowTheme && (button.getToggleState() || down))
    {
        g.setColour (theme.accent.withAlpha (0.22f));
        g.drawRoundedRectangle (bounds.expanded (1.5f), radius + 1.5f, 1.5f);
        g.setColour (theme.accent.withAlpha (0.10f));
        g.drawRoundedRectangle (bounds.expanded (3.0f), radius + 3.0f, 2.0f);
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);

    // Hovered buttons pick up a faint accent wash on glow themes.
    if (glowTheme && highlighted && ! down && ! button.getToggleState())
    {
        g.setColour (theme.accentSoft.withMultipliedAlpha (0.7f));
        g.fillRoundedRectangle (bounds, radius);
    }

    g.setColour (theme.separator);
    g.drawRoundedRectangle (bounds, radius, 1.0f);
}

void AppleLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                       bool, bool)
{
    const auto& theme = ThemeManager::active();
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.getToggleState() ? juce::Colours::white : theme.text);
    g.drawText (button.getButtonText(), button.getLocalBounds(),
                juce::Justification::centred, true);
}

//==============================================================================
juce::Font AppleLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (14.0f).withStyle ("Medium"));
}

void AppleLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height,
                                     bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& theme = ThemeManager::active();
    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    const float radius = juce::jmin (theme.cornerRadius, bounds.getHeight() * 0.5f);

    const bool focused = box.hasKeyboardFocus (false);

    g.setColour (theme.materialStrong);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (focused ? theme.accent : theme.separator);
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    // Faint accent halo around the focused border on glow themes.
    if (focused && theme.glow >= 0.9f)
    {
        g.setColour (theme.accent.withAlpha (0.18f));
        g.drawRoundedRectangle (bounds.expanded (1.5f), radius + 1.5f, 2.0f);
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

    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        const auto rf = r.toFloat();

        // Subtle rounded glow around the highlight on glow themes.
        if (theme.glow >= 0.9f)
        {
            g.setColour (theme.accent.withAlpha (0.20f));
            g.drawRoundedRectangle (rf.expanded (1.5f), 8.5f, 1.5f);
        }

        g.setColour (theme.accent);
        g.fillRoundedRectangle (rf, 7.0f);
    }

    g.setColour (isHighlighted && isActive ? juce::Colours::white
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
