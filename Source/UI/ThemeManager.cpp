#include "ThemeManager.h"

int ThemeManager::idx = 0;

std::array<NeonTheme, ThemeManager::kNumThemes> ThemeManager::all = {{
    {
        "Ocean Blue",
        juce::Colour (0xff0a1128), juce::Colour (0xff001f54),
        juce::Colour (0x33ffffff),
        juce::Colour (0xff34cdfe),
        juce::Colour (0xff5ce1e6),
        juce::Colour (0xff00b4d8),
        juce::Colour (0xff90e0ef),
        juce::Colour (0xffeaf6ff),
        1.0f
    },
    {
        "Magenta Dusk",
        juce::Colour (0xff1a0322), juce::Colour (0xff2d0b3a),
        juce::Colour (0x33ffffff),
        juce::Colour (0xffff4dd6),
        juce::Colour (0xffff77e9),
        juce::Colour (0xffd633ff),
        juce::Colour (0xffffa8f0),
        juce::Colour (0xfffdeaff),
        1.2f
    },
    {
        "Acid Lime",
        juce::Colour (0xff0d1b0d), juce::Colour (0xff14260f),
        juce::Colour (0x33ffffff),
        juce::Colour (0xffb6ff3a),
        juce::Colour (0xffd4ff5c),
        juce::Colour (0xff9ef01a),
        juce::Colour (0xffe4ff9e),
        juce::Colour (0xfff2ffdd),
        1.1f
    },
    {
        "Mono Slate",
        juce::Colour (0xff121212), juce::Colour (0xff1e1e1e),
        juce::Colour (0x26ffffff),
        juce::Colour (0xffe0e0e0),
        juce::Colour (0xffb0b0b0),
        juce::Colour (0xff8a8a8a),
        juce::Colour (0xffffffff),
        juce::Colour (0xfff5f5f5),
        0.6f
    }
}};
