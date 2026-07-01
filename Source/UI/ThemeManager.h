#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

struct NeonTheme
{
    juce::String name;
    juce::Colour bgTop, bgBottom;
    juce::Colour panel;
    juce::Colour knob;
    juce::Colour waveform;
    juce::Colour accent;
    juce::Colour highlight;
    juce::Colour text;
    float glow = 1.0f;
};

/**
    Holds the four built-in neon / glassmorphism themes and the currently
    selected index. Static state is fine here: there is a single UI instance
    and theme selection is a global visual preference.
*/
class ThemeManager
{
public:
    static constexpr int kNumThemes = 4;

    static const std::array<NeonTheme, kNumThemes>& themes() { return all; }
    static int  current()          { return idx; }
    static void setIndex (int i)   { idx = juce::jlimit (0, kNumThemes - 1, i); }
    static const NeonTheme& active() { return all[(size_t) idx]; }

private:
    static std::array<NeonTheme, kNumThemes> all;
    static int idx;
};
