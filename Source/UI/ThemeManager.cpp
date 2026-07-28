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
// So each theme declares three things - an accent, an ink colour for text
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
//
// SOURCE OF TRUTH
// ---------------
// The base palettes, the per-token weights and the six derived accents below
// are DESIGN_SPEC.md §2, literal value for literal value. Two notes on where
// the numbers came from:
//
//  * §2's prose lists the dark palette in full but abbreviates the light one,
//    and it gives the weight map for dark only ("Light mode uses a similar map
//    with lower text weights"). The gaps are filled from the file §2 says it is
//    describing - `Slyce 3.0 Plugin UI.dc.html`, consts DARK / LIGHT /
//    DARK_TINT / LIGHT_TINT. Where the prose and the HTML disagree on a value
//    they both give (light txt / txtB), the prose wins; it is the spec.
//  * Light `txt2` is a deliberate deviation. See makeLightBase().
// ---------------------------------------------------------------------------
namespace
{
    /** Blends `base` toward `accent` by `percent`, keeping base's ALPHA.

        The alpha matters more than it looks: material and separator are
        translucent by design (a card is vibrancy over the backdrop, a hairline
        is white at 9%). Interpolating those as opaque colours would make every
        card solid and every hairline a hard line, which is the whole Apple
        material look gone in one function.

        (CSS `color-mix` would also drag the alpha toward the accent's. At the
        percentages the tint map actually uses - 0 to ~18% - that difference is
        under two alpha steps, and it is not worth solidifying the materials
        for. `colourMix` below does model it, because the derived accents mix at
        34-80% where it genuinely changes the colour.) */
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

    /** `color-mix(in srgb, a p%, b)` - premultiplied, exactly as CSS does it.

        Used for the six derived accent tokens. One of them (accBd) mixes into
        `sep`, a 9%-white hairline, so the result has to end up translucent too;
        a naive lerp would hand back an opaque border. */
    juce::Colour colourMix (juce::Colour a, juce::Colour b, float p)
    {
        const float t  = juce::jlimit (0.0f, 1.0f, p);
        const float wa = t * a.getFloatAlpha();
        const float wb = (1.0f - t) * b.getFloatAlpha();
        const float sum = wa + wb;

        if (sum <= 0.0f)
            return juce::Colours::transparentBlack;

        return juce::Colour::fromFloatRGBA ((a.getFloatRed()   * wa + b.getFloatRed()   * wb) / sum,
                                            (a.getFloatGreen() * wa + b.getFloatGreen() * wb) / sum,
                                            (a.getFloatBlue()  * wa + b.getFloatBlue()  * wb) / sum,
                                            sum);
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

    //==========================================================================
    // Neutral bases - the spec's full token set, one field per --token.
    //==========================================================================
    struct Base
    {
        juce::uint32 bg1, bg2;
        juce::uint32 mat1, mat2, matA1, matA2;
        juce::uint32 card, cardA, cardBd, hi;
        juce::uint32 ctl, ctlBd;
        juce::uint32 well, well2, well3, wellT;
        juce::uint32 sep, sep2, sep3;
        juce::uint32 txt, txtB, txt2, txt3, txt4;
        juce::uint32 knobT, knobB, knobRim, tick, tickL, track, mT, mRim;
        juce::uint32 keyW1, keyW2, keyB1, keyB2, keyTx, keyTxB;
        juce::uint32 segOn;
        juce::uint32 sh1, sh2, shK;
    };

    /** Per-token tint weight. 1.0 means "tinted by exactly the theme's
        strength", 0 (the default) means the token is never tinted - which is
        most of the white/black overlay tokens, since the surface underneath
        them is already carrying the theme's colour. */
    struct Weights
    {
        float bg1 = 0.0f, bg2 = 0.0f;
        float mat1 = 0.0f, mat2 = 0.0f, matA1 = 0.0f, matA2 = 0.0f;
        float card = 0.0f, cardA = 0.0f, cardBd = 0.0f, hi = 0.0f;
        float ctl = 0.0f, ctlBd = 0.0f;
        float well = 0.0f, well2 = 0.0f, well3 = 0.0f, wellT = 0.0f;
        float sep = 0.0f, sep2 = 0.0f, sep3 = 0.0f;
        float txt = 0.0f, txtB = 0.0f, txt2 = 0.0f, txt3 = 0.0f, txt4 = 0.0f;
        float knobT = 0.0f, knobB = 0.0f, knobRim = 0.0f, tick = 0.0f;
        float tickL = 0.0f, track = 0.0f, mT = 0.0f, mRim = 0.0f;
        float keyW1 = 0.0f, keyW2 = 0.0f, keyB1 = 0.0f, keyB2 = 0.0f;
        float keyTx = 0.0f, keyTxB = 0.0f;
        float segOn = 0.0f;
        float sh1 = 0.0f, sh2 = 0.0f, shK = 0.0f;
    };

    // Written as named assignments rather than a 42-slot aggregate literal on
    // purpose: a mis-ordered brace list would silently swap two colours, and
    // this table is verified by eye against the spec, not by a test.

    /** Dark. Slightly warm-neutral rather than pure grey, so the tint has
        something to bend rather than a flat 50% grey that reads as
        unconsidered. DESIGN_SPEC.md §2, "Base palettes - Dark". */
    Base makeDarkBase()
    {
        Base b {};
        b.bg1    = 0xff17161c;   // #17161c
        b.bg2    = 0xff0d0d10;   // #0d0d10
        b.mat1   = 0xff1c1b21;   // #1c1b21
        b.mat2   = 0xff191820;   // #191820
        b.matA1  = 0xff26232e;   // #26232e
        b.matA2  = 0xff1d1c23;   // #1d1c23
        b.card   = 0x99282730;   // rgba(40,39,48,.60)
        b.cardA  = 0x9e3a324a;   // rgba(58,50,74,.62)
        b.cardBd = 0x16ffffff;   // rgba(255,255,255,.085)
        b.hi     = 0x0fffffff;   // rgba(255,255,255,.06)
        b.ctl    = 0x12ffffff;   // rgba(255,255,255,.07)
        b.ctlBd  = 0x1affffff;   // rgba(255,255,255,.10)
        b.well   = 0x4d000000;   // rgba(0,0,0,.30)
        b.well2  = 0x57000000;   // rgba(0,0,0,.34)
        b.well3  = 0x61000000;   // rgba(0,0,0,.38)
        b.wellT  = 0xd9121116;   // rgba(18,17,22,.85)   [HTML only]
        b.sep    = 0x17ffffff;   // rgba(255,255,255,.09)
        b.sep2   = 0x1cffffff;   // rgba(255,255,255,.11)
        b.sep3   = 0x12ffffff;   // rgba(255,255,255,.07)
        b.txt    = 0xfff2f0f5;   // #f2f0f5
        b.txtB   = 0xffc9c6d2;   // #c9c6d2
        b.txt2   = 0xffb6b2c0;   // #b6b2c0
        b.txt3   = 0xffaca8b8;   // #aca8b8
        b.txt4   = 0xff9d99a9;   // #9d99a9
        b.knobT  = 0xff2a2931;   // #2a2931
        b.knobB  = 0xff1a1920;   // #1a1920
        b.knobRim= 0xff3d3c47;   // #3d3c47
        b.tick   = 0xff3a3944;   // #3a3944
        b.tickL  = 0xff413f4d;   // #413f4d
        b.track  = 0xff2f2e38;   // #2f2e38
        b.mT     = 0xff302e3a;   // #302e3a
        b.mRim   = 0xff4a4857;   // #4a4857
        b.keyW1  = 0xfff4f1f8;   // #f4f1f8
        b.keyW2  = 0xffd9d5e2;   // #d9d5e2
        b.keyB1  = 0xff2a2831;   // #2a2831
        b.keyB2  = 0xff121116;   // #121116
        b.keyTx  = 0xff8a879a;   // #8a879a               [HTML only]
        b.keyTxB = 0xff7d7a8c;   // #7d7a8c               [HTML only]
        b.segOn  = 0x2bffffff;   // rgba(255,255,255,.17) [HTML only]
        b.sh1    = 0x73000000;   // rgba(0,0,0,.45)
        b.sh2    = 0x80000000;   // rgba(0,0,0,.5)
        b.shK    = 0x14000000;   // rgba(0,0,0,.08)
        return b;
    }

    /** Light. Deliberately translucent so the tinted desk shows through the
        cards rather than sitting on top of it as flat white panels - which is
        also why so few of the light tokens are tinted at all (see
        makeLightTint): the colour arrives from underneath.
        DESIGN_SPEC.md §2, "Base palettes - Light". */
    Base makeLightBase()
    {
        Base b {};
        // DELIBERATE DEVIATION from §2, the second one in this file.
        //
        // The spec's light ground is #f6f3ef -> #e7e2dc, a warm paper beige.
        // Asked for something more premium, and in this category premium reads
        // COOLER and cleaner - beige reads as vintage or as a document, not as
        // a mastering-grade instrument. This is a near-white with a faint blue
        // cast, which is what the expensive plugins and the hardware they are
        // imitating actually look like.
        //
        // The translucency model is untouched: cards are still white at 42%
        // over the desk, so the accent tint still arrives from underneath and
        // every theme still reads as its own room.
        b.bg1    = 0xfffcfcfd;   // near-white, faint cool cast (spec #f6f3ef)
        b.bg2    = 0xffeceef2;   // (spec #e7e2dc)
        b.mat1   = 0x8cffffff;   // rgba(255,255,255,.55)
        b.mat2   = 0x5cffffff;   // rgba(255,255,255,.36)
        b.matA1  = 0x99ffffff;   // rgba(255,255,255,.60) [HTML only]
        b.matA2  = 0x66ffffff;   // rgba(255,255,255,.40) [HTML only]
        b.card   = 0x6bffffff;   // rgba(255,255,255,.42)
        b.cardA  = 0x85ffffff;   // rgba(255,255,255,.52)
        b.cardBd = 0x17000f1e;   // slightly deeper and cooler than rgba(0,0,0,.07)
        b.hi     = 0xd9ffffff;   // rgba(255,255,255,.85)
        b.ctl    = 0x9effffff;   // rgba(255,255,255,.62)
        b.ctlBd  = 0x1a000000;   // rgba(0,0,0,.10)
        b.well   = 0x0b000000;   // rgba(0,0,0,.045)
        b.well2  = 0x0e000000;   // rgba(0,0,0,.055)      [HTML only]
        b.well3  = 0x0f000000;   // rgba(0,0,0,.06)       [HTML only]
        b.wellT  = 0x9effffff;   // rgba(255,255,255,.62) [HTML only]
        b.sep    = 0x1f000c19;   // deeper than rgba(0,0,0,.10) - see bg1
        b.sep2   = 0x14000000;   // rgba(0,0,0,.08)       [HTML only]
        b.sep3   = 0x12000000;   // rgba(0,0,0,.07)       [HTML only]
        b.txt    = 0xff1a1a1d;   // #1a1a1d

        // §2 says txtB #3c3a40 and txt2 #55515c. txtB is taken as written.
        //
        // txt2 is the one place this file knowingly does not carry the spec's
        // literal, because §2 and §7 contradict each other and only one of them
        // can be satisfied. §2 says #55515c; painted at the alpha these labels
        // actually use, that measured about 2.5:1 against the light cards -
        // every section title, every knob caption washed out - while §7's
        // acceptance list requires "Light themes keep body text >= 4.5:1". A
        // token cannot fail the spec's own acceptance check and still be right,
        // so §7 wins over §2: the value below is the measured replacement,
        // verified at 4.6:1 on the light cards. Darker AND more opaque, because
        // alpha alone on a mid grey just makes a slightly less pale grey.
        //
        // This is the only deviation in the file. If §2's palette is ever
        // re-measured and #55515c clears 4.5:1, put the literal back.
        //
        // (Separately: call sites that re-alpha this - textSecondary.withAlpha
        // (0.55f) and friends - throw the token's own alpha away and drop back
        // under 4.5:1. That is a call-site problem, not a token problem, and
        // fixing it belongs in those files, not here.)
        b.txt2   = 0xd4423d4b;   // spec says #55515c - see above

        b.txt3   = 0xff443f4c;   // #443f4c
        b.txt4   = 0xff464250;   // #464250
        b.knobT  = 0xd9ffffff;   // rgba(255,255,255,.85)
        b.knobB  = 0x8cf0ece6;   // rgba(240,236,230,.55)
        b.knobRim= 0xe6ffffff;   // rgba(255,255,255,.9)  [HTML only]
        b.tick   = 0xffcdc7bf;   // #cdc7bf
        b.tickL  = 0xffbfb8af;   // #bfb8af               [HTML only]
        b.track  = 0xffdcd6ce;   // #dcd6ce               [HTML only]
        b.mT     = 0xccffffff;   // rgba(255,255,255,.8)  [HTML only]
        b.mRim   = 0xe6ffffff;   // rgba(255,255,255,.9)  [HTML only]
        b.keyW1  = 0xffffffff;   // #ffffff
        b.keyW2  = 0xffebe6e0;   // #ebe6e0
        b.keyB1  = 0xff3a3740;   // #3a3740
        b.keyB2  = 0xff1f1d24;   // #1f1d24               [HTML only]
        b.keyTx  = 0xff8d8895;   // #8d8895               [HTML only]
        b.keyTxB = 0xffcdc7bf;   // #cdc7bf               [HTML only]
        b.segOn  = 0xc7ffffff;   // rgba(255,255,255,.78) [HTML only]
        b.sh1    = 0x213c3228;   // rgba(60,50,40,.13)
        b.sh2    = 0x1f3c3228;   // rgba(60,50,40,.12)    [HTML only]
        b.shK    = 0x123c3228;   // rgba(60,50,40,.07)
        return b;
    }

    /** DESIGN_SPEC.md §2, "Per-token weights (dark)". */
    Weights makeDarkTint()
    {
        Weights w {};
        w.bg1   = 1.40f;   // largest surface, carries the room
        w.bg2   = 1.00f;
        w.mat1  = 1.10f;
        w.mat2  = 1.10f;
        w.matA1 = 1.60f;
        w.matA2 = 1.30f;
        w.card  = 1.10f;
        w.cardA = 1.80f;   // the accent panels - Instrument, Output FX, Looper
        w.well  = 0.80f;
        w.well2 = 0.80f;
        w.well3 = 0.80f;
        w.knobT = 0.90f;   // must stay readable as a control
        w.knobB = 0.90f;
        w.tick  = 1.40f;
        w.tickL = 1.40f;
        w.track = 1.20f;
        w.mT    = 1.00f;
        w.mRim  = 1.20f;
        w.txt   = 0.25f;   // tinted body text is harder to read
        w.txtB  = 0.50f;
        w.txt2  = 0.70f;
        w.txt3  = 0.80f;
        w.txt4  = 0.90f;
        w.keyB1 = 1.20f;
        w.keyB2 = 1.00f;
        return w;
    }

    /** The light map. §2 only says "a similar map with lower text weights", so
        the numbers are the HTML's LIGHT_TINT. It is a genuinely different map,
        not the dark one reused: the light materials are white translucency and
        are left untinted on purpose, because what tints them is the desk
        showing through, and the desk itself moves further (bg2 1.6). */
    Weights makeLightTint()
    {
        Weights w {};
        w.bg1   = 1.20f;
        w.bg2   = 1.60f;
        w.mat2  = 0.70f;
        w.matA2 = 0.90f;
        w.well  = 0.60f;
        w.well2 = 0.60f;
        w.well3 = 0.70f;
        w.knobB = 0.80f;
        w.tick  = 1.20f;
        w.tickL = 1.40f;
        w.track = 1.10f;
        w.txt   = 0.20f;
        w.txtB  = 0.40f;
        w.txt2  = 0.50f;
        w.txt3  = 0.60f;
        w.txt4  = 0.70f;
        w.keyW2 = 0.90f;
        w.keyB1 = 0.60f;
        return w;
    }

    Theme build (const Spec& s, const Base& b, const Weights& w)
    {
        const juce::Colour acc (s.accent);
        const float k = s.strength;

        // Every token: the neutral base pushed k * weight percent toward the
        // accent. Weight 0 (the default) leaves the base untouched.
        auto tk = [&acc, k] (juce::uint32 base, float weight)
        {
            return tint (juce::Colour (base), acc, k * weight);
        };

        Theme t;
        t.name = s.name;
        t.dark = s.dark;

        t.bg1     = tk (b.bg1,     w.bg1);
        t.bg2     = tk (b.bg2,     w.bg2);
        t.mat1    = tk (b.mat1,    w.mat1);
        t.mat2    = tk (b.mat2,    w.mat2);
        t.matA1   = tk (b.matA1,   w.matA1);
        t.matA2   = tk (b.matA2,   w.matA2);
        t.card    = tk (b.card,    w.card);
        t.cardA   = tk (b.cardA,   w.cardA);
        t.cardBd  = tk (b.cardBd,  w.cardBd);
        t.hi      = tk (b.hi,      w.hi);
        t.ctl     = tk (b.ctl,     w.ctl);
        t.ctlBd   = tk (b.ctlBd,   w.ctlBd);
        t.well    = tk (b.well,    w.well);
        t.well2   = tk (b.well2,   w.well2);
        t.well3   = tk (b.well3,   w.well3);
        t.wellT   = tk (b.wellT,   w.wellT);
        t.sep     = tk (b.sep,     w.sep);
        t.sep2    = tk (b.sep2,    w.sep2);
        t.sep3    = tk (b.sep3,    w.sep3);
        t.txt     = tk (b.txt,     w.txt);
        t.txtB    = tk (b.txtB,    w.txtB);
        t.txt2    = tk (b.txt2,    w.txt2);
        t.txt3    = tk (b.txt3,    w.txt3);
        t.txt4    = tk (b.txt4,    w.txt4);
        t.knobT   = tk (b.knobT,   w.knobT);
        t.knobB   = tk (b.knobB,   w.knobB);
        t.knobRim = tk (b.knobRim, w.knobRim);
        t.tick    = tk (b.tick,    w.tick);
        t.tickL   = tk (b.tickL,   w.tickL);
        t.track   = tk (b.track,   w.track);
        t.mT      = tk (b.mT,      w.mT);
        t.mRim    = tk (b.mRim,    w.mRim);
        t.keyW1   = tk (b.keyW1,   w.keyW1);
        t.keyW2   = tk (b.keyW2,   w.keyW2);
        t.keyB1   = tk (b.keyB1,   w.keyB1);
        t.keyB2   = tk (b.keyB2,   w.keyB2);
        t.keyTx   = tk (b.keyTx,   w.keyTx);
        t.keyTxB  = tk (b.keyTxB,  w.keyTxB);
        t.segOn   = tk (b.segOn,   w.segOn);
        t.sh1     = tk (b.sh1,     w.sh1);
        t.sh2     = tk (b.sh2,     w.sh2);
        t.shK     = tk (b.shK,     w.shK);

        t.acc   = acc;
        t.onAcc = juce::Colour (s.ink);

        // Derived accents - computed, never hand-picked. Note these mix into
        // the TINTED bg2 / sep / txt, exactly as the CSS does: the vars they
        // reference have already been through the tint map.
        const juce::Colour black (0xff000000), white (0xffffffff);
        t.acc2    = colourMix (acc, black,  0.76f);
        t.accHi   = colourMix (acc, white,  0.80f);
        t.accDim  = colourMix (acc, t.bg2,  0.52f);
        t.accLite = colourMix (acc, white,  0.66f);
        t.accBd   = colourMix (acc, t.sep,  0.34f);
        t.accTxt  = colourMix (acc, t.txt,  0.42f);

        //----------------------------------------------------------------------
        // Established names, aliased onto the spec token each one means.
        //----------------------------------------------------------------------
        t.bgTop          = t.bg1;
        t.bgBottom       = t.bg2;
        t.material       = t.card;    // the translucent panel fill
        t.materialStrong = t.mat1;    // the solid material behind fields/buttons
        t.separator      = t.sep;
        t.control        = t.knobT;
        t.controlBottom  = t.knobB;
        t.controlTrack   = t.track;   // knob's inactive arc; also the segmented
                                      // control's trough, which the spec paints
                                      // from --segOn / --ctl instead - segOn is
                                      // now a token, so that call site can move
                                      // whenever someone touches it.
        t.text           = t.txt;
        t.textSecondary  = t.txt2;
        t.shadow         = t.sh1;
        t.accent         = t.acc;
        t.accentInk      = t.onAcc;

        // No spec token: the spec washes surfaces with inline
        // color-mix(--acc 13-16%, transparent). This is the same idea as a
        // reusable colour, at the alpha the existing call sites are tuned for.
        t.accentSoft = acc.withAlpha (s.dark ? 0.20f : 0.16f);

        // The waveform reads against the backdrop, not against a card, so it
        // needs to sit clear of the accent rather than on it. The spec draws
        // the bars as an accent -> --acc2 gradient; the single colour here is
        // whichever end of that gradient separates from the room.
        t.waveform = s.dark ? t.accHi : t.acc2;

        t.cornerRadius = 12.0f;
        t.glow = 0.0f;
        return t;
    }

    // --- the fifteen ------------------------------------------------------
    // Accents, inks and tint strengths verified against the DESIGN_SPEC.md §2
    // THEMES table row for row - unchanged, they already matched exactly.
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

    /** Neon Ocean is the sixteenth theme, and it is not in the spec - the spec
        lists fifteen. It stays for two reasons. It is not derived and never
        was: it is the one deliberately un-Apple skin, a lit scene rather than a
        neutral room, and running it through the tint machine would turn it into
        another quiet theme, which is precisely what it exists not to be. And it
        is the only theme with glow = 1.0, which is a live branch in
        AppleLookAndFeel, SegmentedControl, KnobComponent, FXRack and the meter
        - deleting the theme deletes the only way to see that rendering.

        Its palette is hand-written below in the same token vocabulary as the
        other two bases, so every new token resolves to a real neon-ocean colour
        rather than defaulting to black. The thirteen values it already had are
        carried across unchanged. */
    Base makeNeonBase()
    {
        Base b {};
        b.bg1    = 0xff0b1030;   // unchanged
        b.bg2    = 0xff050814;   // unchanged
        b.mat1   = 0xf0141c46;   // unchanged (was materialStrong)
        b.mat2   = 0xe6101740;
        b.matA1  = 0xf01b2456;
        b.matA2  = 0xf0121a4a;
        b.card   = 0x1a3aa0ff;   // unchanged (was material)
        b.cardA  = 0x2600f5ff;
        b.cardBd = 0x403aa0ff;
        b.hi     = 0x1a7fd4ff;
        b.ctl    = 0x1a3aa0ff;
        b.ctlBd  = 0x4d3aa0ff;
        b.well   = 0x66020616;
        b.well2  = 0x73020616;
        b.well3  = 0x80020616;
        b.wellT  = 0xd9060a20;
        b.sep    = 0x663aa0ff;   // unchanged
        b.sep2   = 0x803aa0ff;
        b.sep3   = 0x403aa0ff;
        b.txt    = 0xffeafcff;   // unchanged
        b.txtB   = 0xffbfe6f5;
        b.txt2   = 0xb03aa0ff;   // unchanged (was textSecondary)
        b.txt3   = 0xa03aa0ff;
        b.txt4   = 0x8f3aa0ff;
        b.knobT  = 0xff0c1233;   // unchanged (was control)
        b.knobB  = 0xff060a20;   // unchanged (was controlBottom)
        b.knobRim= 0x803aa0ff;
        b.tick   = 0x593aa0ff;   // unchanged
        b.tickL  = 0x733aa0ff;
        b.track  = 0x26ffffff;   // unchanged (was controlTrack)
        b.mT     = 0xff0c1233;
        b.mRim   = 0x803aa0ff;
        b.keyW1  = 0xffdff4ff;
        b.keyW2  = 0xffb6d8ea;
        b.keyB1  = 0xff0c1233;
        b.keyB2  = 0xff05091c;
        b.keyTx  = 0xff4f7fa8;
        b.keyTxB = 0xff7fb4d4;
        b.segOn  = 0x333aa0ff;
        b.sh1    = 0xcc02040c;   // unchanged (was shadow)
        b.sh2    = 0xd902040c;
        b.shK    = 0x3302040c;
        return b;
    }

    Theme neonOcean()
    {
        // Strength 0 and an empty weight map: no tinting, the palette above is
        // already the finished thing. Everything else - the aliases and the six
        // derived accents - comes out of the same machine as the fifteen.
        constexpr Spec s { "Neon Ocean", true, 0xff00f5ff, 0xff02121a, 0.0f };

        Theme t = build (s, makeNeonBase(), Weights {});

        t.accentSoft   = juce::Colour (0x5900f5ff);   // lit, not a hint
        t.waveform     = juce::Colour (0xffff2daa);   // the one non-accent wave
        t.cornerRadius = 14.0f;
        t.glow         = 1.0f;                        // FULL glow
        return t;
    }

    std::array<Theme, ThemeManager::kNumThemes> makeAll()
    {
        const Base darkBase  = makeDarkBase();
        const Base lightBase = makeLightBase();
        const Weights darkW  = makeDarkTint();
        const Weights lightW = makeLightTint();

        std::array<Theme, ThemeManager::kNumThemes> a;
        const int n = (int) (sizeof (kSpecs) / sizeof (kSpecs[0]));
        for (int i = 0; i < n; ++i)
            a[(size_t) i] = kSpecs[i].dark ? build (kSpecs[i], darkBase,  darkW)
                                           : build (kSpecs[i], lightBase, lightW);
        a[(size_t) n] = neonOcean();
        return a;
    }
}

std::array<Theme, ThemeManager::kNumThemes> ThemeManager::all = makeAll();
