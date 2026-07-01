#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ThemeManager.h"
#include <atomic>

/**
    A vertical bar output meter in the house Apple / macOS style.

    Reads an external linear level atomic (0..1, already peak-ish) that is
    written from elsewhere (e.g. the processor). This component never owns nor
    writes the atomic; it only reads it on a ~30 Hz timer and applies meter
    ballistics (fast attack, slow release) plus a slowly falling peak-hold
    marker. All colours are pulled live from ThemeManager::active() in paint()
    so a theme switch restyles it instantly.
*/
class MeterComponent : public juce::Component,
                       private juce::Timer
{
public:
    explicit MeterComponent (std::atomic<float>& levelSource);
    ~MeterComponent() override;

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    std::atomic<float>& level;   // external source, read-only

    float displayed = 0.0f;      // ballistic-smoothed bar value (0..1)
    float peakHold   = 0.0f;     // slowly-falling peak marker (0..1)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterComponent)
};
