#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
    Apple-style design tokens.

    One theme = one cohesive palette in the spirit of macOS / iOS: a soft
    backdrop gradient, translucent "material" cards, hairline separators, a
    single system accent colour, and a restrained (often zero) glow. Components
    read these tokens so the whole UI stays visually consistent.
*/
struct Theme
{
    juce::String name;
    bool  dark = true;

    juce::Colour bgTop, bgBottom;     // window backdrop gradient
    juce::Colour material;            // translucent card fill (vibrancy)
    juce::Colour materialStrong;      // more opaque card fill
    juce::Colour separator;           // hairline dividers / borders

    juce::Colour control;             // knob / pad base fill
    juce::Colour controlTrack;        // inactive ring / track

    juce::Colour accent;              // the single system accent
    juce::Colour accentSoft;          // accent at low alpha (fills, glows)

    juce::Colour waveform;
    juce::Colour text;                // primary label
    juce::Colour textSecondary;       // secondary / muted label
    juce::Colour shadow;              // drop-shadow colour

    float cornerRadius = 12.0f;
    float glow = 0.0f;                // 0 = flat Apple look, >0 = subtle bloom
};

class ThemeManager
{
public:
    static constexpr int kNumThemes = 13;

    static const std::array<Theme, kNumThemes>& themes() { return all; }
    static int  current()          { return idx; }
    static void setIndex (int i)   { idx = juce::jlimit (0, kNumThemes - 1, i); }
    static const Theme& active()   { return all[(size_t) idx]; }

private:
    static std::array<Theme, kNumThemes> all;
    static int idx;
};
