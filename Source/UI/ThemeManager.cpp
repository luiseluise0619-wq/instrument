#include "ThemeManager.h"

int ThemeManager::idx = 0; // default to the flat "Studio" look

// Every palette below is designed as a whole ROOM, not a recolour: the ground
// carries a trace of the accent's hue, the card fills sit a step above it, and
// the waveform colour is chosen to sing against the accent rather than match
// it. Backdrop lighting (aura, counter-light, vignette, grain) is painted from
// these tokens in PluginEditor::paintStudioBackdrop.
std::array<Theme, ThemeManager::kNumThemes> ThemeManager::all = {{
    // ---- Studio Violet (default): plum-black ground, violet accent ---------
    {
        "Studio Violet", true,
        juce::Colour (0xff15111f), juce::Colour (0xff0a0710),   // bg gradient
        juce::Colour (0x0dc4b5fd), juce::Colour (0xf01d1830),   // material / controls
        juce::Colour (0x24c4b5fd),                              // separator
        juce::Colour (0xff231d38), juce::Colour (0x1affffff),   // control / track
        juce::Colour (0xff8b5cf6), juce::Colour (0x338b5cf6),   // accent (violet)
        juce::Colour (0xffc4b5fd),                              // waveform (lilac)
        juce::Colour (0xfff6f4fb), juce::Colour (0x9fb6aecb),   // text / secondary
        juce::Colour (0x80060310),                              // shadow
        12.0f, 0.0f                                             // flat
    },
    // ---- Studio colorways: each gets its OWN tinted ground -----------------
    {
        "Studio Ocean", true,
        juce::Colour (0xff0d1524), juce::Colour (0xff050a13),
        juce::Colour (0x0d93c5fd), juce::Colour (0xf0142036),
        juce::Colour (0x2493c5fd),
        juce::Colour (0xff19273f), juce::Colour (0x1affffff),
        juce::Colour (0xff3b82f6), juce::Colour (0x333b82f6),   // electric blue
        juce::Colour (0xff93c5fd),
        juce::Colour (0xfff2f6fc), juce::Colour (0x9fa6b6ce),
        juce::Colour (0x80020610),
        12.0f, 0.0f
    },
    {
        "Studio Ice", true,
        juce::Colour (0xff0a1720), juce::Colour (0xff040c12),
        juce::Colour (0x0da5f3fc), juce::Colour (0xf0102431),
        juce::Colour (0x24a5f3fc),
        juce::Colour (0xff152c3a), juce::Colour (0x1affffff),
        juce::Colour (0xff22d3ee), juce::Colour (0x3322d3ee),   // cool cyan
        juce::Colour (0xffa5f3fc),
        juce::Colour (0xfff0fafd), juce::Colour (0x9f9db9c6),
        juce::Colour (0x8001080e),
        12.0f, 0.0f
    },
    {
        "Studio Mint", true,
        juce::Colour (0xff0a1a15) , juce::Colour (0xff04100b),
        juce::Colour (0x0d6ee7b7), juce::Colour (0xf0102820),
        juce::Colour (0x246ee7b7),
        juce::Colour (0xff153228), juce::Colour (0x1affffff),
        juce::Colour (0xff34d399), juce::Colour (0x3334d399),   // mint green
        juce::Colour (0xff6ee7b7),
        juce::Colour (0xfff0fbf6), juce::Colour (0x9f9dc0b1),
        juce::Colour (0x80010a06),
        12.0f, 0.0f
    },
    {
        "Studio Amber", true,
        juce::Colour (0xff1b150c) , juce::Colour (0xff100b05),
        juce::Colour (0x0dfcd34d), juce::Colour (0xf02a2013),
        juce::Colour (0x24fcd34d),
        juce::Colour (0xff33271a), juce::Colour (0x1affffff),
        juce::Colour (0xfff59e0b), juce::Colour (0x33f59e0b),   // warm amber
        juce::Colour (0xfffcd34d),
        juce::Colour (0xfffdf7ec), juce::Colour (0x9fc7b394),
        juce::Colour (0x800b0602),
        12.0f, 0.0f
    },
    {
        "Studio Rose", true,
        juce::Colour (0xff1d1015) , juce::Colour (0xff11070b),
        juce::Colour (0x0dfda4af), juce::Colour (0xf02c1720),
        juce::Colour (0x24fda4af),
        juce::Colour (0xff351d27), juce::Colour (0x1affffff),
        juce::Colour (0xfff43f5e), juce::Colour (0x33f43f5e),   // rose red
        juce::Colour (0xfffda4af),
        juce::Colour (0xfffdf1f4), juce::Colour (0x9fc9a3ad),
        juce::Colour (0x800c0308),
        12.0f, 0.0f
    },
    {
        "Studio Gold", true,
        juce::Colour (0xff18140b) , juce::Colour (0xff0d0a04),
        juce::Colour (0x0de6cf85), juce::Colour (0xf0261f11),
        juce::Colour (0x24e6cf85),
        juce::Colour (0xff2e2617), juce::Colour (0x1affffff),
        juce::Colour (0xffd4af37), juce::Colour (0x33d4af37),   // luxe gold
        juce::Colour (0xffe6cf85),
        juce::Colour (0xfffbf6e9), juce::Colour (0x9fc0b28c),
        juce::Colour (0x80080502),
        12.0f, 0.0f
    },
    {
        "Studio Mono", true,
        juce::Colour (0xff141417) , juce::Colour (0xff08080a),
        juce::Colour (0x0dffffff), juce::Colour (0xf01f1f24),
        juce::Colour (0x24ffffff),
        juce::Colour (0xff26262c), juce::Colour (0x1affffff),
        juce::Colour (0xfff5f5f7), juce::Colour (0x26f5f5f7),   // pure monochrome
        juce::Colour (0xffb9b9c1),
        juce::Colour (0xfff7f7f9), juce::Colour (0x9fadadb6),
        juce::Colour (0x80000000),
        12.0f, 0.0f
    },
    // ---- Sunset: dusk purple ground, ember accent, gold highlight ----------
    {
        "Sunset", true,
        juce::Colour (0xff1e1020), juce::Colour (0xff0d0610),
        juce::Colour (0x0fffb27a), juce::Colour (0xf02c1526),
        juce::Colour (0x2eff8a5b),
        juce::Colour (0xff36192c), juce::Colour (0x1fffffff),
        juce::Colour (0xffff6b35), juce::Colour (0x3aff6b35),   // ember orange
        juce::Colour (0xffffd166),                              // gold crest
        juce::Colour (0xfffff2ea), juce::Colour (0xa5d3a898),
        juce::Colour (0x8c0a0208),
        13.0f, 0.35f
    },
    // ---- Emerald: deep jade ground, emerald accent, champagne wave --------
    {
        "Emerald", true,
        juce::Colour (0xff081a16), juce::Colour (0xff03100c),
        juce::Colour (0x0f6ee7b7), juce::Colour (0xf00d2a21),
        juce::Colour (0x2e34d399),
        juce::Colour (0xff113329), juce::Colour (0x1fffffff),
        juce::Colour (0xff10b981), juce::Colour (0x3a10b981),   // emerald
        juce::Colour (0xffe8d9a0),                              // champagne
        juce::Colour (0xffeefaf5), juce::Colour (0xa596bfb0),
        juce::Colour (0x8c010a07),
        13.0f, 0.3f
    },
    // ---- Royal Velvet: indigo velvet ground, lilac accent, gold wave ------
    {
        "Royal Velvet", true,
        juce::Colour (0xff171335) , juce::Colour (0xff0a0819),
        juce::Colour (0x11c7b3ff), juce::Colour (0xf0221c4a),
        juce::Colour (0x33c7b3ff),
        juce::Colour (0xff2a2258), juce::Colour (0x1fffffff),
        juce::Colour (0xffa78bfa), juce::Colour (0x3aa78bfa),   // lilac
        juce::Colour (0xfff0c674),                              // antique gold
        juce::Colour (0xfff5f2ff), juce::Colour (0xa5b0a7d6),
        juce::Colour (0x8c050318),
        14.0f, 0.4f
    },
    // ---- Carbon: true black, single red accent, surgical contrast ---------
    {
        "Carbon", true,
        juce::Colour (0xff0e0e0f), juce::Colour (0xff030303),
        juce::Colour (0x0dff8a8a), juce::Colour (0xf01a1a1c),
        juce::Colour (0x26ffffff),
        juce::Colour (0xff232326), juce::Colour (0x1affffff),
        juce::Colour (0xffef4444), juce::Colour (0x33ef4444),   // signal red
        juce::Colour (0xfffca5a5),
        juce::Colour (0xfff8f8f8), juce::Colour (0x9fa8a8ac),
        juce::Colour (0x99000000),
        10.0f, 0.0f
    },
    // ---- Neon Ocean (cyber synthwave scene: navy sea + neon cyan/pink) ----
    {
        "Neon Ocean", true,
        juce::Colour (0xff0b1030), juce::Colour (0xff050814),   // bg gradient (navy)
        juce::Colour (0x1a3aa0ff), juce::Colour (0xf0141c46),
        juce::Colour (0x663aa0ff),                              // separator (soft blue)
        juce::Colour (0xff0c1233), juce::Colour (0x26ffffff),   // control / track
        juce::Colour (0xff00f5ff), juce::Colour (0x5900f5ff),   // accent (neon cyan)
        juce::Colour (0xffff2daa),                              // waveform (hot pink)
        juce::Colour (0xffeafcff), juce::Colour (0xb03aa0ff),   // text / secondary
        juce::Colour (0xcc02040c),                              // shadow
        14.0f, 1.0f                                             // FULL glow
    },
    // ---- Silver (light): warm paper, system blue -------------------------
    {
        "Silver", false,
        juce::Colour (0xfffbfbfd), juce::Colour (0xffe8eaf0),   // bg gradient
        juce::Colour (0xbdffffff), juce::Colour (0xf7ffffff),   // material
        juce::Colour (0x1a0a1633),                              // separator
        juce::Colour (0xffffffff), juce::Colour (0x140a1633),   // control / track
        juce::Colour (0xff007aff), juce::Colour (0x26007aff),   // accent (system blue)
        juce::Colour (0xff1d4ed8),                              // waveform (ink blue)
        juce::Colour (0xff14161c), juce::Colour (0x8c4a5060),   // text / secondary
        juce::Colour (0x24101828),                              // shadow
        13.0f, 0.0f
    },
    // ---- Graphite (dark) -------------------------------------------------
    {
        "Graphite", true,
        juce::Colour (0xff1d1d20), juce::Colour (0xff0a0a0b),
        juce::Colour (0x14ffffff), juce::Colour (0xf0333338),   // solid control fill
        juce::Colour (0x30ffffff),
        juce::Colour (0xff2c2c2e), juce::Colour (0x1fffffff),
        juce::Colour (0xff0a84ff), juce::Colour (0x330a84ff),   // system blue (dark)
        juce::Colour (0xffe5e5ea),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99ffffff),
        juce::Colour (0x80000000),
        13.0f, 0.12f
    },
    // ---- Midnight (deep blue) -------------------------------------------
    {
        "Midnight", true,
        juce::Colour (0xff0b1020), juce::Colour (0xff05070f),
        juce::Colour (0x149ad9ff), juce::Colour (0xf01c2742),   // solid control fill
        juce::Colour (0x3364d2ff),
        juce::Colour (0xff17203a), juce::Colour (0x1fffffff),
        juce::Colour (0xff64d2ff), juce::Colour (0x3364d2ff),   // system teal
        juce::Colour (0xff9ad9ff),
        juce::Colour (0xfff2f6ff), juce::Colour (0x99cfe0ff),
        juce::Colour (0x8c000308),
        14.0f, 0.18f
    },
    // ---- Space Gray (neutral) -------------------------------------------
    {
        "Space Gray", true,
        juce::Colour (0xff2b2b2f), juce::Colour (0xff151517),
        juce::Colour (0x14e7d6ff), juce::Colour (0xf044444a),   // solid control fill
        juce::Colour (0x30ffffff),
        juce::Colour (0xff3a3a3d), juce::Colour (0x1fffffff),
        juce::Colour (0xffbf5af2), juce::Colour (0x33bf5af2),   // system purple
        juce::Colour (0xffe7d6ff),
        juce::Colour (0xfff5f5f7), juce::Colour (0x99ffffff),
        juce::Colour (0x80000000),
        13.0f, 0.1f
    }
}};
