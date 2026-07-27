#include "ThemeManager.h"

int ThemeManager::idx = 0; // default to Studio Violet

// ---------------------------------------------------------------------------
// Every chrome token is DERIVED, not hand-picked.
//
// The old table hand-wrote fifteen themes that shared one set of greys and
// differed only in their accent. That is a recolour: switch theme and the room
// stays exactly the same, one highlight changes. A theme should feel like a
// different physical instrument.
//
// So each theme now declares three things - an accent, an ink colour for text
// sitting ON the accent, and a tint strength - and every other token is the
// neutral base pushed some distance toward that accent. The distance differs
// per token, which is the part that matters: the backdrop moves furthest
// (weight 1.4) because it is the largest surface and carries the room's
// colour, the knob face barely moves (0.9) because it has to stay readable as
// a control, and body text moves almost not at all (0.25) because tinted text
// is just harder to read.
//
// Graphite declares tint 0 and therefore comes out as pure neutral greys - it
// is the "no theme" theme, and it is the one case where sharing chrome is the
// point.
// ---------------------------------------------------------------------------
namespace
{
    /** Blends `base` toward `accent` by `percent`, keeping base's ALPHA.

        The alpha matters more than it looks: material and separator are
        translucent by design (a card is vibrancy over the backdrop, a hairline
        is white at 9%). Interpolating those as opaque colours would make every
        card solid and every hairline a hard line, which is the whole Apple
        material look gone in one function. */
    juce::Colour tint (juce::Colour base, juce::Colour accent, float percent)
    {
        if (percent <= 0.0f) return base;
        const float p = juce::jlimit (0.0f, 1.0f, percent * 0.01f);
        return juce::Colour::fromFloatRGBA (
            base.getFloatRed()   + (accent.getFloatRed()   - base.getFloatRed())   * p,
            base.getFloatGreen() + (accent.getFloatGreen() - base.getFloatGreen()) * p,
            base.getFloatBlue()  + (accent.getFloatBlue()  - base.getFloatBlue())  * p,
            base.getFloatAlpha());
    }

    /** One theme, described the way a person would describe it. */
    struct Spec
    {
        const char*   name;
        bool          dark;
        juce::uint32  accent;
        juce::uint32  ink;      // text that sits ON the accent
        float         strength; // 0 = neutral chrome, ~10 = strongly tinted
    };

    // --- neutral bases -----------------------------------------------------
    // Dark. Slightly warm-neutral rather than pure grey, so the tint has
    // something to bend rather than a flat 50% grey that reads as unconsidered.
    struct Base
    {
        juce::uint32 bgTop, bgBottom, material, materialStrong, separator,
                     control, controlBottom, controlTrack, tick,
                     text, textSecondary, shadow;
    };

    constexpr Base kDark = {
        0xff17161c, 0xff0d0d10,          // backdrop gradient
        0x99282730, 0xf01c1b21,          // translucent card / solid control fill
        0x17ffffff,                      // hairline
        0xff2a2931, 0xff1a1920,          // knob face top / bottom
        0x2fffffff,                      // inactive track
        0x3affffff,                      // tick marks
        0xfff2f0f5, 0xb6b6b2c0,          // primary / secondary text
        0x73000000
    };

    // Light. Deliberately translucent so the tinted desk shows through the
    // cards rather than sitting on top of it as flat white panels.
    constexpr Base kLight = {
        0xfff6f3ef, 0xffe7e2dc,
        0x6bffffff, 0xf5ffffff,
        0x1a000000,
        0xffffffff, 0xf0f0ece6,
        0x14000000,
        0x33000000,
        0xff1a1a1d, 0x9955515c,
        0x21322814
    };

    // --- per-token tint weights -------------------------------------------
    // A weight of 1.0 means "tinted by exactly the theme's strength".
    constexpr float wBgTop     = 1.40f;   // largest surface, carries the room
    constexpr float wBgBottom  = 1.00f;
    constexpr float wMaterial  = 1.10f;
    constexpr float wMatStrong = 1.10f;
    constexpr float wSeparator = 1.20f;
    constexpr float wControl   = 0.90f;   // must stay readable as a control
    constexpr float wTrack     = 1.20f;
    constexpr float wTick      = 1.40f;
    constexpr float wText      = 0.25f;   // tinted body text is harder to read
    constexpr float wTextSec   = 0.70f;

    Theme build (const Spec& s)
    {
        const Base& b = s.dark ? kDark : kLight;
        const juce::Colour acc (s.accent);
        const float k = s.strength;

        Theme t;
        t.name = s.name;
        t.dark = s.dark;

        t.bgTop          = tint (juce::Colour (b.bgTop),          acc, k * wBgTop);
        t.bgBottom       = tint (juce::Colour (b.bgBottom),       acc, k * wBgBottom);
        t.material       = tint (juce::Colour (b.material),       acc, k * wMaterial);
        t.materialStrong = tint (juce::Colour (b.materialStrong), acc, k * wMatStrong);
        t.separator      = tint (juce::Colour (b.separator),      acc, k * wSeparator);
        t.control        = tint (juce::Colour (b.control),        acc, k * wControl);
        t.controlBottom  = tint (juce::Colour (b.controlBottom),  acc, k * wControl);
        t.controlTrack   = tint (juce::Colour (b.controlTrack),   acc, k * wTrack);
        t.tick           = tint (juce::Colour (b.tick),           acc, k * wTick);
        t.text           = tint (juce::Colour (b.text),           acc, k * wText);
        t.textSecondary  = tint (juce::Colour (b.textSecondary),  acc, k * wTextSec);
        t.shadow         = juce::Colour (b.shadow);

        t.accent    = acc;
        t.accentInk = juce::Colour (s.ink);
        t.accentSoft = acc.withAlpha (s.dark ? 0.20f : 0.16f);
        // The waveform reads against the backdrop, not against a card, so it
        // needs to sit clear of the accent rather than on it.
        t.waveform = s.dark ? acc.brighter (0.45f) : acc.darker (0.25f);

        t.cornerRadius = 12.0f;
        t.glow = 0.0f;
        return t;
    }

    // --- the fifteen ------------------------------------------------------
    constexpr Spec kSpecs[] = {
        { "Studio Violet", true,  0xffbf5af2, 0xff1c0d26,  7.0f },
        { "Signal",        true,  0xffff3d7f, 0xff2c0616,  7.0f },
        { "Acid",          true,  0xffc2f24a, 0xff141c07,  6.0f },
        { "Mint",          true,  0xff2fd6a3, 0xff04231a,  7.0f },
        { "Graphite",      true,  0xffb9b6c4, 0xff1a191f,  0.0f },
        { "Cobalt",        true,  0xff3d8bff, 0xff04142e,  9.0f },
        { "Indigo",        true,  0xff7b78f5, 0xff0c0a2e,  9.0f },
        { "Aqua",          true,  0xff32ade6, 0xff031c27,  8.0f },
        { "Ember",         true,  0xffff7a3d, 0xff2a0f05,  8.0f },
        { "Sunset",        true,  0xffffb340, 0xff2a1a02,  8.0f },
        { "Clay",          true,  0xffd98a6a, 0xff2a1611, 10.0f },
        { "Paper",         false, 0xff5b3df2, 0xfff5f3ff,  5.0f },
        { "Bone",          false, 0xffc8452b, 0xfffff4f1,  6.0f },
        { "Sand",          false, 0xffa8752a, 0xfffff8ee,  7.0f },
        { "Snow",          false, 0xff0a72e8, 0xfff2f8ff,  4.0f },
    };

    /** Neon Ocean is not derived and never was. It is the one deliberately
        un-Apple skin - a lit scene rather than a neutral room - and running it
        through the tint machine would turn it into another quiet theme, which
        is precisely what it exists not to be. Kept hand-written, and kept. */
    Theme neonOcean()
    {
        Theme t;
        t.name = "Neon Ocean";
        t.dark = true;
        t.bgTop          = juce::Colour (0xff0b1030);
        t.bgBottom       = juce::Colour (0xff050814);
        t.material       = juce::Colour (0x1a3aa0ff);
        t.materialStrong = juce::Colour (0xf0141c46);
        t.separator      = juce::Colour (0x663aa0ff);
        t.control        = juce::Colour (0xff0c1233);
        t.controlBottom  = juce::Colour (0xff060a20);
        t.controlTrack   = juce::Colour (0x26ffffff);
        t.tick           = juce::Colour (0x593aa0ff);
        t.accent         = juce::Colour (0xff00f5ff);
        t.accentInk      = juce::Colour (0xff02121a);
        t.accentSoft     = juce::Colour (0x5900f5ff);
        t.waveform       = juce::Colour (0xffff2daa);
        t.text           = juce::Colour (0xffeafcff);
        t.textSecondary  = juce::Colour (0xb03aa0ff);
        t.shadow         = juce::Colour (0xcc02040c);
        t.cornerRadius   = 14.0f;
        t.glow           = 1.0f;              // FULL glow
        return t;
    }

    std::array<Theme, ThemeManager::kNumThemes> makeAll()
    {
        std::array<Theme, ThemeManager::kNumThemes> a;
        const int n = (int) (sizeof (kSpecs) / sizeof (kSpecs[0]));
        for (int i = 0; i < n; ++i)
            a[(size_t) i] = build (kSpecs[i]);
        a[(size_t) n] = neonOcean();
        return a;
    }
}

std::array<Theme, ThemeManager::kNumThemes> ThemeManager::all = makeAll();
