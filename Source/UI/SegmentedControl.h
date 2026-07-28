#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ThemeManager.h"
#include <functional>

/**
    An iOS-style segmented picker (design spec 4.2 / 4.5).

    The spec asks for segmented controls - not drop-downs - wherever a parameter
    has a handful of mutually exclusive values that are all worth reading at a
    glance: "Slice by (Transient|Beats)", "Grid (4|8|16|32)",
    "Wave (Saw|Square|Sine|Tri)". A combo box hides every option but one behind
    a click; a segment strip shows the whole vocabulary and marks the live value
    with the accent, which is exactly the accent rule from spec 1 ("accent means
    currently active").

    Visually: a rounded track (radius 10 - the spec's button/field radius) filled
    with the theme's inactive-track token and edged with a hairline separator,
    carrying a pill of theme.accent over the selected segment with its label in
    theme.accentInk. Unselected labels sit in theme.textSecondary and lift to
    theme.text under the mouse.

    The pill SLIDES between segments rather than jumping. The slide is driven off
    the wall clock (juce::Time::getMillisecondCounterHiRes), so it takes the same
    170ms whatever the frame rate, and the timer that drives it exists only while
    the pill is actually moving - this plugin has real performance history with
    animations left running behind the scenes, so nothing here ticks at rest.

    Labels are never clipped or ellipsised: the font is measured against the
    exact face it is drawn with and stepped down (to a 9px floor) until the
    longest label fits inside one segment.

    All colours come live from ThemeManager::active(), so a theme switch restyles
    it instantly and it reads correctly in both light and dark themes.
*/
class SegmentedControl : public juce::Component,
                         private juce::Timer
{
public:
    SegmentedControl();
    ~SegmentedControl() override;

    /** Fills the strip. Safe to call again to re-label or resize the control;
        the selection is kept if it is still in range, otherwise it falls back to
        the first segment (or nothing at all when the list is empty). Never fires
        onChange and never animates - a strip that slid on every refresh would
        read as a glitch. */
    void setItems (const juce::StringArray& newItems);

    const juce::StringArray& getItems() const noexcept   { return items; }
    int  getNumItems() const noexcept                    { return items.size(); }

    /** The live segment, or -1 while the strip is empty. */
    int  getSelectedIndex() const noexcept               { return selectedIndex; }

    /** The live segment's label, or an empty string while the strip is empty. */
    juce::String getSelectedText() const;

    /** Selects a segment (clamped to the available range). onChange fires only
        when the notification type asks for it; sendNotificationAsync defers the
        call to the message loop, everything else calls straight out. */
    void setSelectedIndex (int newIndex, juce::NotificationType notification);

    /** Fired with the new index whenever the selection actually changes and the
        notification type allows it - including user clicks and arrow keys. */
    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (juce::Component::FocusChangeType) override;
    void focusLost   (juce::Component::FocusChangeType) override;

    void enablementChanged() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;

    /** Which segment contains this local x, or -1 if the strip is empty. */
    int  segmentAt (int x) const;

    /** Re-measures the label font for the current size and item list. */
    void updateLabelFont();

    /** Starts (or re-aims) the pill slide toward the current selection. */
    void startSlide();

    /** Drops the pill onto the current selection with no animation. */
    void snapPill();

    juce::StringArray items;

    int selectedIndex = -1;
    int hoveredIndex  = -1;

    // Pill position in SEGMENT units (2.5 = halfway between segments 2 and 3),
    // so the geometry stays correct however the strip is resized mid-slide.
    float  pillPos    = 0.0f;
    float  slideFrom  = 0.0f;
    float  slideTo    = 0.0f;
    double slideStart = 0.0;   // wall clock ms at the start of the slide

    float labelFontSize = 11.0f;   // measured in updateLabelFont()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedControl)
};
