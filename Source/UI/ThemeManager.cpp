#include "ThemeManager.h"

int ThemeManager::idx = 0;

// Apple's rule, applied literally: the CHROME is neutral (near-black greys in
// dark mode, paper greys in light mode) and colour appears only where it means
// something - the accent. Picking a theme here is like picking an accent colour
// in System Settings: the room stays the same, the highlights change.
//
// Greys are the macOS system greys (#1c1c1e / #2c2c2e / #3a3a3c / #48484a) and
// the accents are the system colours (systemBlue, systemPurple, systemPink...).
namespace
{
    // Shared neutral chrome for every dark theme.
    constexpr juce::uint32 kBgTop      = 0xff1b1b1d;
    constexpr juce::uint32 kBgBottom   = 0xff0b0b0c;
    constexpr juce::uint32 kMaterial   = 0x0dffffff;   // translucent card
    constexpr juce::uint32 kMatStrong  = 0xf02c2c2e;   // solid control fill
    constexpr juce::uint32 kSeparator  = 0x1fffffff;   // hairline
    constexpr juce::uint32 kControl    = 0xff48484a;   // knob face (light grey)
    constexpr juce::uint32 kTrack      = 0x1fffffff;   // inactive ring
    constexpr juce::uint32 kText       = 0xfff2f2f7;
    constexpr juce::uint32 kTextSec    = 0x8cf2f2f7;
    constexpr juce::uint32 kShadow     = 0x80000000;
}

std::array<Theme, ThemeManager::kNumThemes> ThemeManager::all = {{
    // ---- Studio Violet (default) -------------------------------------------
    {
        "Studio Violet", true,
        juce::Colour (kBgTop), juce::Colour (kBgBottom),
        juce::Colour (kMaterial), juce::Colour (kMatStrong),
        juce::Colour (kSeparator),
        juce::Colour (kControl), juce::Colour (kTrack),
        juce::Colour (0xffbf5af2), juce::Colour (0x33bf5af2),   // systemPurple
        juce::Colour (0xffd8a7ff),
        juce::Colour (kText), juce::Colour (kTextSec),
        juce::Colour (kShadow),
        12.0f, 0.0f
    },
    { "Studio Ocean", true,
        juce::Colour (kBgTop), juce::Colour (kBgBottom),
        juce::Colour (kMaterial), juce::Colour (kMatStrong),
        juce::Colour (kSeparator),
        juce::Colour (kControl), juce::Colour (kTrack),
        juce::Colour (0xff0a84ff), juce::Colour (0x330a84ff),   // systemBlue
        juce::Colour (0xff9ecbff),
        juce::Colour (kText), juce::Colour (kTextSec),
        juce::Colour (kShadow),
        12.0f, 0.0f },
    { "Studio Ice", true,
        juce::Colour (kBgTop), juce::Colour (kBgBottom),
        juce::Colour (kMaterial), juce::Colour (kMatStrong),
        juce::Colour (kSeparator),
        juce::Colour (kControl), juce::Colour (kTrack),
        juce::Colour (0xff64d2ff), juce::Colour (0x3364d2ff),   // systemTeal
        juce::Colour (0xffb8e9ff),
        juce::Colour (kText), juce::Colour (kTextSec),
        juce::Colour (kShadow),
        12.0f, 0.0f },
    { "Studio Mint", true,
        juce::Colour (kBgTop), juce::Colour (kBgBottom),
        juce::Colour (kMaterial), juce::Colour (kMatStrong),
        juce::Colour (kSeparator),
        juce::Colour (kControl), juce::Colour (kTrack),
        juce::Colour (0xff30d158), juce::Colour (0x3330d158),   // systemGreen
        juce::Colour (0xff9ff0b4),
        juce::Colour (kText), juce::Colour (kTextSec),
        juce::Colour (kShadow),
        12.0f, 0.0f },
    { "Studio Amber", true,
        juce::Colour (kBgTop), juce::Colour (kBgBottom),
        juce::Colour (kMaterial), juce::Colour (kMatStrong),
        juce::Colour (kSeparator),
        juce::Colour (kControl), juce::Colour (kTrack),
        juce::Colour (0xffff9f0a), juce::Colour (0x33ff9f0a),   // systemOrange
        juce::Colour (0xffffd08a),
        juce::Colour (kText), juce::Colour (kTextSec),
        juce::Colour (kShadow),
        12.0f, 0.0f },
    { "Studio Rose", true,
        juce::Colour (kBgTop), juce::Colour (kBgBottom),
        juce::Colour (kMaterial), juce::Colour (kMatStrong),
        juce::Colour (kSeparator),
        juce::Colour (kControl), juce::Colour (kTrack),
        juce::Colour (0xffff375f), juce::Colour (0x33ff375f),   // systemPink
        juce::Colour (0xffff9fb2),
        juce::Colour (kText), juce::Colour (kTextSec),
        juce::Colour (kShadow),
        12.0f, 0.0f },
    { "Studio Gold", true,
        juce::Colour (kBgTop), juce::Colour (kBgBottom),
        juce::Colour (kMaterial), juce::Colour (kMatStrong),
        juce::Colour (kSeparator),
        juce::Colour (kControl), juce::Colour (kTrack),
        juce::Colour (0xffffd60a), juce::Colour (0x33ffd60a),   // systemYellow
        juce::Colour (0xffffe98a),
        juce::Colour (kText), juce::Colour (kTextSec),
        juce::Colour (kShadow),
        12.0f, 0.0f },
    { "Studio Mono", true,
        juce::Colour (kBgTop), juce::Colour (kBgBottom),
        juce::Colour (kMaterial), juce::Colour (kMatStrong),
        juce::Colour (kSeparator),
        juce::Colour (kControl), juce::Colour (kTrack),
        juce::Colour (0xfff2f2f7), juce::Colour (0x26f2f2f7),   // monochrome
        juce::Colour (0xffc7c7cc),
        juce::Colour (kText), juce::Colour (kTextSec),
        juce::Colour (kShadow),
        12.0f, 0.0f },
    { "Studio Red", true,
        juce::Colour (kBgTop), juce::Colour (kBgBottom),
        juce::Colour (kMaterial), juce::Colour (kMatStrong),
        juce::Colour (kSeparator),
        juce::Colour (kControl), juce::Colour (kTrack),
        juce::Colour (0xffff453a), juce::Colour (0x33ff453a),   // systemRed
        juce::Colour (0xffffa39d),
        juce::Colour (kText), juce::Colour (kTextSec),
        juce::Colour (kShadow),
        12.0f, 0.0f },
    { "Studio Indigo", true,
        juce::Colour (kBgTop), juce::Colour (kBgBottom),
        juce::Colour (kMaterial), juce::Colour (kMatStrong),
        juce::Colour (kSeparator),
        juce::Colour (kControl), juce::Colour (kTrack),
        juce::Colour (0xff5e5ce6), juce::Colour (0x335e5ce6),   // systemIndigo
        juce::Colour (0xffb0afff),
        juce::Colour (kText), juce::Colour (kTextSec),
        juce::Colour (kShadow),
        12.0f, 0.0f },
    // ---- Graphite: the same chrome one step lighter (a "desk lamp" room) ---
    {
        "Graphite", true,
        juce::Colour (0xff242426), juce::Colour (0xff121213),
        juce::Colour (0x12ffffff), juce::Colour (0xf0333336),
        juce::Colour (0x26ffffff),
        juce::Colour (0xff48484a), juce::Colour (0x26ffffff),
        juce::Colour (0xff0a84ff), juce::Colour (0x330a84ff),
        juce::Colour (0xffdcdce0),
        juce::Colour (0xfff2f2f7), juce::Colour (0x99f2f2f7),
        juce::Colour (kShadow),
        12.0f, 0.0f
    },
    // ---- Midnight: near-black with the faintest blue cast -----------------
    {
        "Midnight", true,
        juce::Colour (0xff14161c), juce::Colour (0xff08090d),
        juce::Colour (0x0dffffff), juce::Colour (0xf0242832),
        juce::Colour (0x1fffffff),
        juce::Colour (0xff32363f), juce::Colour (0x1fffffff),
        juce::Colour (0xff64d2ff), juce::Colour (0x3364d2ff),
        juce::Colour (0xffb8e9ff),
        juce::Colour (0xfff2f4f8), juce::Colour (0x8cf2f4f8),
        juce::Colour (0x8c000000),
        12.0f, 0.0f
    },
    // ---- Silver: light mode, Apple's grouped-background greys -------------
    {
        "Silver", false,
        juce::Colour (0xfff7f7f9), juce::Colour (0xffe6e6eb),
        juce::Colour (0xc6ffffff), juce::Colour (0xfaffffff),
        juce::Colour (0x1c000000),
        juce::Colour (0xffffffff), juce::Colour (0x14000000),
        juce::Colour (0xff007aff), juce::Colour (0x26007aff),   // systemBlue
        juce::Colour (0xff2c6bd8),
        juce::Colour (0xff1c1c1e), juce::Colour (0x8c3a3a3c),
        juce::Colour (0x1f000000),
        12.0f, 0.0f
    },
    // ---- Snow: light mode, purple accent ---------------------------------
    {
        "Snow", false,
        juce::Colour (0xfffafafc), juce::Colour (0xffeaeaef),
        juce::Colour (0xc6ffffff), juce::Colour (0xfaffffff),
        juce::Colour (0x1c000000),
        juce::Colour (0xffffffff), juce::Colour (0x14000000),
        juce::Colour (0xffaf52de), juce::Colour (0x26af52de),   // systemPurple
        juce::Colour (0xff7b3fa0),
        juce::Colour (0xff1c1c1e), juce::Colour (0x8c3a3a3c),
        juce::Colour (0x1f000000),
        12.0f, 0.0f
    },
    // ---- Neon Ocean: the one deliberately un-Apple skin (scene backdrop) --
    {
        "Neon Ocean", true,
        juce::Colour (0xff0b1030), juce::Colour (0xff050814),
        juce::Colour (0x1a3aa0ff), juce::Colour (0xf0141c46),
        juce::Colour (0x663aa0ff),
        juce::Colour (0xff0c1233), juce::Colour (0x26ffffff),
        juce::Colour (0xff00f5ff), juce::Colour (0x5900f5ff),
        juce::Colour (0xffff2daa),
        juce::Colour (0xffeafcff), juce::Colour (0xb03aa0ff),
        juce::Colour (0xcc02040c),
        14.0f, 1.0f                                             // FULL glow
    }
}};
