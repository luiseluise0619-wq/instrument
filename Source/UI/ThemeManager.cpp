#include "ThemeManager.h"

int ThemeManager::idx = 0; // default to the synthwave "Neon Wave" theme

std::array<Theme, ThemeManager::kNumThemes> ThemeManager::all = {{
    // ---- Neon Ocean (cyber synthwave: deep navy sea + neon cyan/purple) ---
    // Palette per design spec: #050814 navy, #00F5FF cyan, #B026FF purple,
    // #FF2DAA hot pink, #3AA0FF soft blue glow.
    {
        "Neon Ocean", true,
        juce::Colour (0xff0b1030), juce::Colour (0xff050814),   // bg gradient (navy)
        juce::Colour (0x1a3aa0ff), juce::Colour (0x2e3aa0ff),   // material (soft blue glass)
        juce::Colour (0x3d3aa0ff),                              // separator (soft blue)
        juce::Colour (0xff0c1233), juce::Colour (0x26ffffff),   // control / track
        juce::Colour (0xff00f5ff), juce::Colour (0x5900f5ff),   // accent (neon cyan)
        juce::Colour (0xffff2daa),                              // waveform (hot pink)
        juce::Colour (0xffeafcff), juce::Colour (0xb03aa0ff),   // text / secondary
        juce::Colour (0xcc02040c),                              // shadow
        14.0f, 1.0f                                             // FULL glow
    },
    // ---- Silver (light) --------------------------------------------------
    {
        "Silver", false,
        juce::Colour (0xfff5f5f7), juce::Colour (0xffe9e9ec),   // bg gradient
        juce::Colour (0xb3ffffff), juce::Colour (0xf2ffffff),   // material
        juce::Colour (0x14000000),                              // separator
        juce::Colour (0xffffffff), juce::Colour (0x14000000),   // control / track
        juce::Colour (0xff007aff), juce::Colour (0x26007aff),   // accent (system blue)
        juce::Colour (0xff1d1d1f),                              // waveform (graphite)
        juce::Colour (0xff1d1d1f), juce::Colour (0x8c1d1d1f),   // text / secondary
        juce::Colour (0x1f000000),                              // shadow
        13.0f, 0.0f
    },
    // ---- Graphite (dark) -------------------------------------------------
    {
        "Graphite", true,
        juce::Colour (0xff1c1c1e), juce::Colour (0xff0a0a0b),
        juce::Colour (0x14ffffff), juce::Colour (0x26ffffff),
        juce::Colour (0x1fffffff),
        juce::Colour (0xff2c2c2e), juce::Colour (0x1fffffff),
        juce::Colour (0xff0a84ff), juce::Colour (0x330a84ff),   // system blue (dark)
        juce::Colour (0xffe5e5ea),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99ffffff),
        juce::Colour (0x66000000),
        13.0f, 0.12f
    },
    // ---- Midnight (deep blue) -------------------------------------------
    {
        "Midnight", true,
        juce::Colour (0xff0b1020), juce::Colour (0xff05070f),
        juce::Colour (0x14ffffff), juce::Colour (0x24ffffff),
        juce::Colour (0x1fffffff),
        juce::Colour (0xff17203a), juce::Colour (0x1fffffff),
        juce::Colour (0xff64d2ff), juce::Colour (0x3364d2ff),   // system teal
        juce::Colour (0xff9ad9ff),
        juce::Colour (0xfff2f6ff), juce::Colour (0x99cfe0ff),
        juce::Colour (0x80000000),
        14.0f, 0.18f
    },
    // ---- Space Gray (neutral) -------------------------------------------
    {
        "Space Gray", true,
        juce::Colour (0xff2a2a2d), juce::Colour (0xff161618),
        juce::Colour (0x14ffffff), juce::Colour (0x26ffffff),
        juce::Colour (0x1fffffff),
        juce::Colour (0xff3a3a3d), juce::Colour (0x1fffffff),
        juce::Colour (0xffbf5af2), juce::Colour (0x33bf5af2),   // system purple
        juce::Colour (0xffe7d6ff),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99ffffff),
        juce::Colour (0x66000000),
        13.0f, 0.1f
    }
}};
