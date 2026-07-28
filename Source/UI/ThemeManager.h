#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

/**
    Apple-style design tokens - the full token set from DESIGN_SPEC.md §2.

    One theme = one cohesive palette in the spirit of macOS / iOS: a soft
    backdrop gradient, translucent "material" cards, hairline separators, a
    single system accent colour, and a restrained (often zero) glow. Components
    read these tokens so the whole UI stays visually consistent.

    A theme is declared by three things only - an accent, an ink colour for text
    sitting ON the accent, and a tint strength. Every chrome token below is the
    neutral base pushed some distance toward that accent (see ThemeManager.cpp),
    and the six acc* tokens are computed from the accent, never hand-picked.

    TWO NAMES, ONE COLOUR
    ---------------------
    The fields are in two blocks. The first block carries the spec's own token
    names (bg1, mat1, txt2, knobT ...) so new code can be read next to the spec
    and the CSS it came from. The second block is the older, longer-form names
    that a dozen files already use; each one is an alias assigned the same value
    as its spec token, listed in the comment beside it. Nothing was renamed or
    removed - `theme.material` and `theme.card` are the same colour, spelled two
    ways. `tick` needs no alias: the old field name and the spec token agree.
*/
struct Theme
{
    juce::String name;
    bool  dark = true;

    //==========================================================================
    // Spec tokens - DESIGN_SPEC.md §2 "Base palettes".
    //==========================================================================

    juce::Colour bg1, bg2;              // desk gradient, top -> bottom
    juce::Colour mat1, mat2;            // solid material fills
    juce::Colour matA1, matA2;          // accent-forward material fills
    juce::Colour card, cardA;           // translucent panel fill / accent panel
    juce::Colour cardBd;                // panel hairline border
    juce::Colour hi;                    // inset top highlight on a panel
    juce::Colour ctl, ctlBd;            // toolbar control fill / border
    juce::Colour well, well2, well3;    // recessed wells, increasing depth
    juce::Colour wellT;                 // opaque well (tooltip / popover base)
    juce::Colour sep, sep2, sep3;       // hairlines: normal / strong / faint
    juce::Colour txt;                   // primary label
    juce::Colour txtB;                  // bright-but-not-primary label
    juce::Colour txt2, txt3, txt4;      // secondary -> quaternary labels
    juce::Colour knobT, knobB;          // knob body gradient top / bottom
    juce::Colour knobRim;               // knob inset rim highlight
    juce::Colour tick;                  // knob tick-ring marks
    juce::Colour tickL;                 // lit / major tick marks
    juce::Colour track;                 // inactive ring / track
    juce::Colour mT, mRim;              // meter body / meter rim
    juce::Colour keyW1, keyW2;          // white key gradient
    juce::Colour keyB1, keyB2;          // black key gradient
    juce::Colour keyTx, keyTxB;         // key letter on white / on black
    juce::Colour segOn;                 // selected segment pill
    juce::Colour sh1, sh2, shK;         // shadows: panel / deep / key

    juce::Colour acc;                   // the single system accent
    juce::Colour onAcc;                 // text/graphics sitting ON the accent

    // Derived accents - computed from acc (and bg2 / sep / txt), §2.
    juce::Colour acc2;                  // acc 76% + black   - gradient shadow end
    juce::Colour accHi;                 // acc 80% + white    - lit accent
    juce::Colour accDim;                // acc 52% + bg2      - receded accent
    juce::Colour accLite;               // acc 66% + white    - washed accent
    juce::Colour accBd;                 // acc 34% + sep      - accent hairline
    juce::Colour accTxt;                // acc 42% + txt      - accent-tinted label

    //==========================================================================
    // Established names. Each is an alias of the spec token named beside it -
    // same colour, older spelling. Kept so existing call sites keep compiling
    // and keep reading the spec's value.
    //==========================================================================

    juce::Colour bgTop, bgBottom;     // = bg1 / bg2       window backdrop gradient
    juce::Colour material;            // = card            translucent card fill (vibrancy)
    juce::Colour materialStrong;      // = mat1            more opaque card fill
    juce::Colour separator;           // = sep             hairline dividers / borders

    juce::Colour control;             // = knobT           knob / pad base fill (gradient top)
    juce::Colour controlBottom;       // = knobB           knob gradient bottom
    juce::Colour controlTrack;        // = track           inactive ring / track
                                      //   (`tick` above serves both names)

    juce::Colour accent;              // = acc             the single system accent
    juce::Colour accentInk;           // = onAcc           text/graphics sitting ON the accent
    juce::Colour accentSoft;          // accent at low alpha (fills, glows) - no spec token

    juce::Colour waveform;            // = accHi (dark) / acc2 (light)
    juce::Colour text;                // = txt             primary label
    juce::Colour textSecondary;       // = txt2            secondary / muted label
    juce::Colour shadow;              // = sh1             drop-shadow colour

    float cornerRadius = 12.0f;
    float glow = 0.0f;                // 0 = flat Apple look, >0 = subtle bloom
};

class ThemeManager
{
public:
    static constexpr int kNumThemes = 16;   // 15 derived + Neon Ocean

    static const std::array<Theme, kNumThemes>& themes() { return all; }
    static int  current()          { return idx; }
    static void setIndex (int i)   { idx = juce::jlimit (0, kNumThemes - 1, i); }
    static const Theme& active()   { return all[(size_t) idx]; }

private:
    static std::array<Theme, kNumThemes> all;
    static int idx;
};
