#include "ThemeManager.h"

int ThemeManager::idx = 0; // default to the flat "Studio" look

std::array<Theme, ThemeManager::kNumThemes> ThemeManager::all = {{
    // ---- Studio (default): near-black, violet accent, flat pro look --------
    // Modelled on modern commercial instruments: matte panels, one restrained
    // accent, zero glow. The artwork skins below stay available as options.
    {
        "Studio Violet", true,
        juce::Colour (0xff101014), juce::Colour (0xff09090b),   // bg gradient
        juce::Colour (0x0affffff), juce::Colour (0xf018181d),   // material / controls
        juce::Colour (0x1effffff),                              // separator
        juce::Colour (0xff1c1c22), juce::Colour (0x1affffff),   // control / track
        juce::Colour (0xff8b5cf6), juce::Colour (0x338b5cf6),   // accent (violet)
        juce::Colour (0xffa78bfa),                              // waveform (soft violet)
        juce::Colour (0xfff5f5f7), juce::Colour (0x99b9b9c1),   // text / secondary
        juce::Colour (0x66000000),                              // shadow
        12.0f, 0.0f                                             // flat
    },
    // ---- Studio colorways: same matte black ground, one accent swapped ----
    {
        "Studio Ocean", true,
        juce::Colour (0xff101014), juce::Colour (0xff09090b),
        juce::Colour (0x0affffff), juce::Colour (0xf018181d),
        juce::Colour (0x1effffff),
        juce::Colour (0xff1c1c22), juce::Colour (0x1affffff),
        juce::Colour (0xff3b82f6), juce::Colour (0x333b82f6),   // electric blue
        juce::Colour (0xff93c5fd),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99b9b9c1),
        juce::Colour (0x66000000),
        12.0f, 0.0f
    },
    {
        "Studio Ice", true,
        juce::Colour (0xff101014), juce::Colour (0xff09090b),
        juce::Colour (0x0affffff), juce::Colour (0xf018181d),
        juce::Colour (0x1effffff),
        juce::Colour (0xff1c1c22), juce::Colour (0x1affffff),
        juce::Colour (0xff22d3ee), juce::Colour (0x3322d3ee),   // cool cyan
        juce::Colour (0xffa5f3fc),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99b9b9c1),
        juce::Colour (0x66000000),
        12.0f, 0.0f
    },
    {
        "Studio Mint", true,
        juce::Colour (0xff101014), juce::Colour (0xff09090b),
        juce::Colour (0x0affffff), juce::Colour (0xf018181d),
        juce::Colour (0x1effffff),
        juce::Colour (0xff1c1c22), juce::Colour (0x1affffff),
        juce::Colour (0xff34d399), juce::Colour (0x3334d399),   // mint green
        juce::Colour (0xff6ee7b7),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99b9b9c1),
        juce::Colour (0x66000000),
        12.0f, 0.0f
    },
    {
        "Studio Amber", true,
        juce::Colour (0xff101014), juce::Colour (0xff09090b),
        juce::Colour (0x0affffff), juce::Colour (0xf018181d),
        juce::Colour (0x1effffff),
        juce::Colour (0xff1c1c22), juce::Colour (0x1affffff),
        juce::Colour (0xfff59e0b), juce::Colour (0x33f59e0b),   // warm amber
        juce::Colour (0xfffcd34d),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99b9b9c1),
        juce::Colour (0x66000000),
        12.0f, 0.0f
    },
    {
        "Studio Rose", true,
        juce::Colour (0xff101014), juce::Colour (0xff09090b),
        juce::Colour (0x0affffff), juce::Colour (0xf018181d),
        juce::Colour (0x1effffff),
        juce::Colour (0xff1c1c22), juce::Colour (0x1affffff),
        juce::Colour (0xfff43f5e), juce::Colour (0x33f43f5e),   // rose red
        juce::Colour (0xfffda4af),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99b9b9c1),
        juce::Colour (0x66000000),
        12.0f, 0.0f
    },
    {
        "Studio Gold", true,
        juce::Colour (0xff101014), juce::Colour (0xff09090b),
        juce::Colour (0x0affffff), juce::Colour (0xf018181d),
        juce::Colour (0x1effffff),
        juce::Colour (0xff1c1c22), juce::Colour (0x1affffff),
        juce::Colour (0xffd4af37), juce::Colour (0x33d4af37),   // luxe gold
        juce::Colour (0xffe6cf85),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99b9b9c1),
        juce::Colour (0x66000000),
        12.0f, 0.0f
    },
    {
        "Studio Mono", true,
        juce::Colour (0xff101014), juce::Colour (0xff09090b),
        juce::Colour (0x0affffff), juce::Colour (0xf018181d),
        juce::Colour (0x1effffff),
        juce::Colour (0xff1c1c22), juce::Colour (0x1affffff),
        juce::Colour (0xfff5f5f7), juce::Colour (0x26f5f5f7),   // pure monochrome
        juce::Colour (0xffb9b9c1),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99b9b9c1),
        juce::Colour (0x66000000),
        12.0f, 0.0f
    },
    // ---- Neon Ocean (cyber synthwave: deep navy sea + neon cyan/purple) ---
    // Palette per design spec: #050814 navy, #00F5FF cyan, #B026FF purple,
    // #FF2DAA hot pink, #3AA0FF soft blue glow.
    {
        "Neon Ocean", true,
        juce::Colour (0xff0b1030), juce::Colour (0xff050814),   // bg gradient (navy)
        // material stays glassy; materialStrong (buttons/combos) is a solid
        // indigo so controls clearly stand out from the dark cards.
        juce::Colour (0x1a3aa0ff), juce::Colour (0xf0141c46),
        juce::Colour (0x663aa0ff),                              // separator (soft blue)
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
        juce::Colour (0x14ffffff), juce::Colour (0xf0333338),   // solid control fill
        juce::Colour (0x30ffffff),
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
        juce::Colour (0x14ffffff), juce::Colour (0xf01c2742),   // solid control fill
        juce::Colour (0x3364d2ff),
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
        juce::Colour (0x14ffffff), juce::Colour (0xf044444a),   // solid control fill
        juce::Colour (0x30ffffff),
        juce::Colour (0xff3a3a3d), juce::Colour (0x1fffffff),
        juce::Colour (0xffbf5af2), juce::Colour (0x33bf5af2),   // system purple
        juce::Colour (0xffe7d6ff),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99ffffff),
        juce::Colour (0x66000000),
        13.0f, 0.1f
    }
}};
