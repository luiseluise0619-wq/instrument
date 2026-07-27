#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "ThemeManager.h"

/**
    Ambient mode: a full-panel visualiser for leaving the plugin running on a
    spare screen.

    Eight looks, all drawn as vectors against the active theme's accent. The
    constraint that shaped every one of them is that this runs next to a live
    audio thread on a laptop: no bitmaps, no blurs, no per-frame shadows, and
    a repaint budget small enough that leaving it open all day is free. Where
    the design called for a CSS 3D transform, the projection is computed
    directly - a perspective divide is three multiplies, whereas a blur is a
    full-surface convolution.

    Click anywhere to leave.
*/
class AmbientPanel : public juce::Component,
                     private juce::Timer
{
public:
    AmbientPanel();
    ~AmbientPanel() override;

    static constexpr int kNumLooks = 8;
    static const char* lookName (int i);

    /** Voice name, category and key shown in the metadata row. */
    void setNowPlaying (juce::String voice, juce::String category, juce::String key);
    void setBpm (double b)          { bpm = b; }

    /** Called when the panel is shown or hidden, so the timer only runs while
        anything can actually be seen. */
    void setActive (bool shouldBeActive);

    std::function<void()> onExit;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void step (int delta);

    void drawBars      (juce::Graphics&, juce::Rectangle<float>);
    void drawWavetable (juce::Graphics&, juce::Rectangle<float>);
    void drawTunnel    (juce::Graphics&, juce::Rectangle<float>);
    void drawRibbon    (juce::Graphics&, juce::Rectangle<float>);
    void drawGrid      (juce::Graphics&, juce::Rectangle<float>);
    void drawPetals    (juce::Graphics&, juce::Rectangle<float>);
    void drawGlobe     (juce::Graphics&, juce::Rectangle<float>);
    void drawRain      (juce::Graphics&, juce::Rectangle<float>);

    int    look = 0;
    double phase = 0.0;         // seconds since the look was entered
    float  fadeIn = 0.0f;       // 0..1, the 500 ms entry fade
    double bpm = 124.0;

    juce::String voiceName { "Supersaw Lead" }, categoryName { "LEAD" }, keyName { "D minor" };

    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::Label      lookLabel;
    // One dot per look, so a specific one can be reached without stepping.
    juce::TextButton dot[kNumLooks];

    juce::Rectangle<int> dotRow;

    // Painting the backdrop live was the single mistake that made this hang:
    // a full-canvas linear gradient plus two 1200px RADIAL gradients, thirty
    // times a second, is several million shaded pixels per frame in a
    // software renderer. Both are static, so both are drawn once and blitted.
    juce::Image backdropCache;      // the desk gradient, panel-sized
    juce::Image blobCache;          // one small soft blob, scaled up per frame
    int cachedTheme = -1;
    void rebuildCaches();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmbientPanel)
};
