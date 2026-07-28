#include "SegmentedControl.h"

#include <cmath>

namespace
{
    // Spec 1: radius 10 for buttons and fields, which is what a segment strip
    // is. The pill sits inside the track by kPillInset on every side.
    constexpr float kTrackRadius   = 10.0f;
    constexpr float kPillInset     =  2.0f;
    constexpr float kFocusInset    =  1.5f;   // room for the focus ring
    constexpr float kLabelPadding  =  6.0f;   // per side, inside one segment

    // Spec 1: labels never go below 10px, but a four-way strip in a narrow
    // context panel would rather shrink than clip, so 9px is the hard floor.
    constexpr float kMinFontSize   =  9.0f;
    constexpr float kMaxFontSize   = 12.5f;

    // The disabled look the rest of the editor uses (PluginEditor's dim()).
    constexpr float kDisabledAlpha = 0.38f;

    // Long enough to read as movement, short enough to feel instant. The macOS
    // switch in AppleLookAndFeel uses 180ms; the pill travels further, so it
    // sits at the fast end of the spec's 160-180ms window.
    constexpr double kSlideMs      = 170.0;

    /** The same cubic-bezier(.32, .72, 0, 1) easing the macOS-style switches
        use, so every sliding element in the plugin moves with one hand: quick
        off the mark, long settle. Solved for t by bisection - there is no
        closed form - which is a handful of multiplies once per frame. */
    float slideEase (float x) noexcept
    {
        constexpr float x1 = 0.32f, y1 = 0.72f;
        constexpr float x2 = 0.0f,  y2 = 1.0f;

        auto bezier = [] (float a, float b, float t)
        {
            const float u = 1.0f - t;
            return 3.0f * u * u * t * a + 3.0f * u * t * t * b + t * t * t;
        };

        float lo = 0.0f, hi = 1.0f, t = x;
        for (int i = 0; i < 16; ++i)
        {
            t = (lo + hi) * 0.5f;
            if (bezier (x1, x2, t) < x)
                lo = t;
            else
                hi = t;
        }

        return juce::jlimit (0.0f, 1.0f, bezier (y1, y2, t));
    }

    /** The face the labels are actually drawn with. Selected segments are
        semibold (spec 4.2: "weight 600"); everything is MEASURED against the
        semibold face, because it is the wider of the two - fit that and the
        regular weight cannot clip either. */
    juce::Font labelFace (float size, bool strong)
    {
        return strong ? juce::Font (juce::FontOptions (size).withStyle ("Semibold"))
                      : juce::Font (juce::FontOptions (size));
    }
}

//==============================================================================
SegmentedControl::SegmentedControl()
{
    // Focusable so the arrow keys can drive it, but it deliberately does NOT
    // grab focus on click: the editor hands keyboard focus to the on-screen
    // keyboard so typing plays notes, and a picker that stole it every time you
    // changed a grid division would silently kill that.
    setWantsKeyboardFocus (true);
}

SegmentedControl::~SegmentedControl()
{
    stopTimer();
}

//==============================================================================
void SegmentedControl::setItems (const juce::StringArray& newItems)
{
    items = newItems;

    if (items.isEmpty())
        selectedIndex = -1;
    else
        selectedIndex = juce::jlimit (0, items.size() - 1,
                                      selectedIndex < 0 ? 0 : selectedIndex);

    hoveredIndex = -1;

    updateLabelFont();
    snapPill();      // re-labelling is not a selection change: no slide
    repaint();
}

juce::String SegmentedControl::getSelectedText() const
{
    return juce::isPositiveAndBelow (selectedIndex, items.size())
               ? items[selectedIndex] : juce::String();
}

void SegmentedControl::setSelectedIndex (int newIndex, juce::NotificationType notification)
{
    const int clamped = items.isEmpty() ? -1
                                        : juce::jlimit (0, items.size() - 1, newIndex);

    if (clamped == selectedIndex)
        return;

    selectedIndex = clamped;
    startSlide();
    repaint();

    if (notification == juce::dontSendNotification || ! onChange)
        return;

    if (notification == juce::sendNotificationAsync)
    {
        // The listener may well delete or rebuild whatever triggered this, so
        // the async path goes through a SafePointer rather than capturing this.
        juce::Component::SafePointer<SegmentedControl> safe (this);
        const int sent = selectedIndex;

        juce::MessageManager::callAsync ([safe, sent]
        {
            if (auto* self = safe.getComponent())
                if (self->onChange)
                    self->onChange (sent);
        });
    }
    else
    {
        onChange (selectedIndex);
    }
}

//==============================================================================
void SegmentedControl::resized()
{
    updateLabelFont();
}

int SegmentedControl::segmentAt (int x) const
{
    const int n = items.size();
    if (n <= 0)
        return -1;

    const float inner = juce::jmax (1.0f, (float) getWidth() - 2.0f * (kFocusInset + kPillInset));
    const float rel   = ((float) x - (kFocusInset + kPillInset)) / inner;

    return juce::jlimit (0, n - 1, (int) std::floor (rel * (float) n));
}

void SegmentedControl::updateLabelFont()
{
    float size = juce::jlimit (kMinFontSize, kMaxFontSize, (float) getHeight() * 0.42f);

    const int n = items.size();
    if (n > 0 && getWidth() > 0)
    {
        const float segW = ((float) getWidth() - 2.0f * (kFocusInset + kPillInset)) / (float) n;
        const float room = juce::jmax (4.0f, segW - 2.0f * kLabelPadding);

        // Step down until the LONGEST label fits its own segment, measured
        // against the face it is drawn with. Shrink, never ellipsise: a grid
        // division reading "3..." is worse than a small "32".
        auto widest = [this, n] (float s)
        {
            int w = 0;
            const auto face = labelFace (s, true);
            for (int i = 0; i < n; ++i)
                w = juce::jmax (w, juce::GlyphArrangement::getStringWidthInt (face, items[i]));
            return (float) w;
        };

        while (size > kMinFontSize && widest (size) > room)
            size -= 0.5f;
    }

    labelFontSize = juce::jmax (kMinFontSize, size);
}

//==============================================================================
void SegmentedControl::startSlide()
{
    const float target = (float) juce::jmax (0, selectedIndex);

    if (! isShowing())
    {
        // Nothing to watch: land immediately rather than animating off-screen.
        slideTo = target;
        snapPill();
        return;
    }

    slideFrom  = pillPos;
    slideTo    = target;
    slideStart = juce::Time::getMillisecondCounterHiRes();

    if (std::abs (slideTo - slideFrom) < 0.001f)
    {
        snapPill();
        return;
    }

    if (! isTimerRunning())
        startTimerHz (60);
}

void SegmentedControl::snapPill()
{
    const float target = (float) juce::jmax (0, selectedIndex);

    pillPos   = target;
    slideFrom = target;
    slideTo   = target;

    stopTimer();   // at rest = no timer, ever
}

void SegmentedControl::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float  u   = (float) juce::jlimit (0.0, 1.0, (now - slideStart) / kSlideMs);

    pillPos = slideFrom + (slideTo - slideFrom) * slideEase (u);

    if (u >= 1.0f)
    {
        pillPos = slideTo;
        stopTimer();   // the pill has landed: stop burning frames
    }

    repaint();
}

void SegmentedControl::visibilityChanged()
{
    // Never animate while off-screen (the panels here swap in and out a lot).
    if (! isShowing() && isTimerRunning())
        snapPill();
}

void SegmentedControl::enablementChanged()
{
    if (! isEnabled())
        hoveredIndex = -1;

    repaint();
}

//==============================================================================
void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    // Disabled components still receive mouse events in JUCE, so the guard has
    // to live here.
    if (! isEnabled())
        return;

    const int hit = segmentAt (e.x);
    if (hit >= 0)
        setSelectedIndex (hit, juce::sendNotificationSync);
}

void SegmentedControl::mouseDrag (const juce::MouseEvent& e)
{
    // Dragging across the strip keeps picking, the way the iOS control does.
    if (! isEnabled())
        return;

    if (getLocalBounds().contains (e.getPosition()))
        mouseDown (e);
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const int over = isEnabled() ? segmentAt (e.x) : -1;

    if (over != hoveredIndex)
    {
        hoveredIndex = over;
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    if (hoveredIndex >= 0)
    {
        hoveredIndex = -1;
        repaint();
    }
}

bool SegmentedControl::keyPressed (const juce::KeyPress& key)
{
    if (! isEnabled() || items.size() < 2)
        return false;

    if (key.isKeyCode (juce::KeyPress::leftKey))
    {
        setSelectedIndex (juce::jmax (0, selectedIndex - 1), juce::sendNotificationSync);
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::rightKey))
    {
        setSelectedIndex (juce::jmin (items.size() - 1, selectedIndex + 1),
                          juce::sendNotificationSync);
        return true;
    }

    return false;
}

void SegmentedControl::focusGained (juce::Component::FocusChangeType)   { repaint(); }
void SegmentedControl::focusLost   (juce::Component::FocusChangeType)   { repaint(); }

//==============================================================================
void SegmentedControl::paint (juce::Graphics& g)
{
    const auto& theme = ThemeManager::active();

    auto bounds = getLocalBounds().toFloat();
    if (bounds.getWidth() < 4.0f || bounds.getHeight() < 4.0f)
        return;   // guard against zero-size bounds

    // Disabled dims the WHOLE paint, matching PluginEditor's dim() helper.
    const float dimAmount = isEnabled() ? 1.0f : kDisabledAlpha;
    auto ink = [dimAmount] (juce::Colour c) { return c.withMultipliedAlpha (dimAmount); };

    const bool glowTheme = theme.glow >= 0.9f;

    auto track = bounds.reduced (kFocusInset);
    const float radius = juce::jmin (kTrackRadius, track.getHeight() * 0.5f);

    //--------------------------------------------------------------------------
    // Track: the inactive-track token, hairline edged. On the neon themes the
    // edge becomes a soft accent rim, the way the cards and meter do.
    g.setColour (ink (theme.controlTrack));
    g.fillRoundedRectangle (track, radius);

    if (glowTheme)
    {
        g.setColour (ink (theme.accent.withAlpha (0.10f)));
        g.drawRoundedRectangle (track.expanded (1.0f), radius + 1.0f, 2.5f);
        g.setColour (ink (theme.accent.withAlpha (0.28f)));
        g.drawRoundedRectangle (track.reduced (0.5f), radius, 1.0f);
    }
    else
    {
        g.setColour (ink (theme.separator));
        g.drawRoundedRectangle (track.reduced (0.5f), radius, 1.0f);
    }

    const int n = items.size();
    if (n <= 0)
        return;

    auto inner = track.reduced (kPillInset);
    if (inner.getWidth() < 2.0f || inner.getHeight() < 2.0f)
        return;

    const float segW  = inner.getWidth() / (float) n;
    const float pillR = juce::jmax (2.0f, radius - kPillInset);

    //--------------------------------------------------------------------------
    // The selected pill, wherever the slide has it right now.
    if (selectedIndex >= 0)
    {
        auto pill = juce::Rectangle<float> (inner.getX() + segW * pillPos, inner.getY(),
                                            segW, inner.getHeight());

        // Bloom under the pill on glow themes; a cheap offset shadow otherwise.
        if (theme.glow > 0.0f)
        {
            g.setColour (ink (theme.accent.withAlpha (juce::jlimit (0.0f, 0.45f,
                                                                    0.18f * theme.glow))));
            g.fillRoundedRectangle (pill.expanded (2.5f), pillR + 2.5f);
        }
        else
        {
            g.setColour (ink (theme.shadow.withAlpha (0.14f)));
            g.fillRoundedRectangle (pill.translated (0.0f, 1.0f), pillR);
        }

        g.setColour (ink (theme.accent));
        g.fillRoundedRectangle (pill, pillR);
    }

    //--------------------------------------------------------------------------
    // Labels. Each one blends toward the on-accent ink by however much of the
    // pill is currently over it, so mid-slide nothing is unreadable.
    for (int i = 0; i < n; ++i)
    {
        auto cell = juce::Rectangle<float> (inner.getX() + segW * (float) i, inner.getY(),
                                            segW, inner.getHeight())
                        .reduced (kLabelPadding, 0.0f);

        const float covered = selectedIndex < 0
                                  ? 0.0f
                                  : juce::jlimit (0.0f, 1.0f,
                                                  1.0f - std::abs (pillPos - (float) i));

        const juce::Colour base = (i == hoveredIndex && i != selectedIndex)
                                      ? theme.text          // hover lifts it
                                      : theme.textSecondary;

        g.setColour (ink (base.interpolatedWith (theme.accentInk, covered)));
        g.setFont (labelFace (labelFontSize, covered > 0.5f));
        g.drawText (items[i], cell, juce::Justification::centred, false);
    }

    //--------------------------------------------------------------------------
    // Keyboard focus ring: the arrows are live, so say so.
    if (hasKeyboardFocus (false) && isEnabled())
    {
        g.setColour (theme.accent.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.75f), radius + kFocusInset, 1.5f);
    }
}
