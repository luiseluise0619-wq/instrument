#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    The radius scale (design spec section 1):
    16 panels / 14 large controls / 10 buttons and fields / 8-9 chips / 999 pills.

    One radius for every element was the giveaway that the corners were never
    designed - a 32px toolbar button and a 150px panel cannot share a curve and
    read as the same family. ThemeManager still carries a single `cornerRadius`
    token (other components rely on it), so these live here instead: derived
    locally, no theme change needed.
*/
namespace AppleRadius
{
    inline constexpr float panel  =  16.0f;
    inline constexpr float large  =  14.0f;   // large controls / floating menus
    inline constexpr float button =  10.0f;   // buttons and combo fields
    inline constexpr float chip   =   9.0f;
    inline constexpr float pill   = 999.0f;   // clamped to half the height
}

/**
    Shared look-and-feel that gives buttons, combo boxes and menus a macOS / iOS
    feel: rounded "material" fills, hairline borders, a single accent colour,
    and clean SF-style typography. All colours are pulled live from the active
    Theme, so switching themes restyles everything without re-instantiating.
*/
class AppleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AppleLookAndFeel();

    /** Radius for one control, honouring per-component overrides: set the
        boolean property "pill", "chip" or "largeControl" on a component to
        move it off `defaultRadius`. Always clamped to half the height, so a
        pill comes out as a true stadium at any size. */
    static float radiusFor (juce::Component&, juce::Rectangle<float> bounds,
                            float defaultRadius);

    /** A combo can ask for a multi-column popup by setting a "menuColumns"
        property on itself. The theme picker needs it: sixteen rows in one
        column runs off a short plugin window. */
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&,
                                                             juce::Label&) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override;
    int  getPopupMenuBorderSize() override { return 6; }

    void drawTooltip (juce::Graphics&, const juce::String& text,
                      int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                                           juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
};
