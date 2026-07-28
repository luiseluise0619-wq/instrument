#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

class VocalChopAudioProcessor;

/**
    The 24-bar live output scope (design spec 4.5, row 3 - "Scope", 150px wide).

    A scrolling level history of the plugin's output: every timer frame the
    newest bar takes the peak of whatever the audio thread has written into the
    processor's scope ring since the last frame, and the whole row shifts one
    bar to the left. The right-most bar is therefore "now" and is drawn
    emphasised; older bars fade back as they scroll away.

    This component never writes to the processor - it only reads the existing
    scope ring (getScopeRing() / getScopeWritePos()), which is a read-only,
    non-consuming source, so it cannot steal peaks from MeterComponent.

    Every bar carries its own ballistics (instant attack, eased release) so the
    row never flickers, and the timer runs at 30 Hz and is stopped completely
    whenever the panel is not visible.

    All colours come live from ThemeManager::active(); nothing is hard-coded, so
    a theme switch restyles it instantly and it reads correctly in both light
    and dark themes. Only the bars and their sunken well are drawn here - the
    card, border and caption around the panel belong to PluginEditor.
*/
class ScopePanel : public juce::Component,
                   private juce::Timer
{
public:
    explicit ScopePanel (VocalChopAudioProcessor& processor);
    ~ScopePanel() override;

    void paint (juce::Graphics& g) override;

    /** Stops the timer the moment the panel goes off-screen: this plugin has
        had real performance problems from panels animating while hidden. */
    void visibilityChanged() override;

private:
    static constexpr int kNumBars = 24;

    void timerCallback() override;

    VocalChopAudioProcessor& proc;

    std::array<float, kNumBars> target {};    // raw level history (0..1)
    std::array<float, kNumBars> shown  {};    // ballistic-smoothed, drawn

    int   lastScopePos   = -1;    // processor ring write position last frame
    float lastDrawnPeak  = 1.0f;  // idle-skip: loudest bar at the last repaint

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopePanel)
};
