// Offline arrangement renderer: builds the demo tracks for the product page
// by running one plugin instance per part and summing them.
//
// Everything here is real plugin output - the same synth engine, arpeggiator,
// sidechain pump, portamento and limiter the buyer gets. Nothing is sweetened
// afterwards beyond one gain stage into the plugin's own limiter.
//
//   SlyceDemoRender <out.wav> [edm|hiphop|pop|funk|showcase] [soloInstrument]
#include "PluginProcessor.h"
#include "DSP/Limiter.h"
#include <cstdio>
#include <map>
#include <vector>

namespace
{
constexpr double kSr    = 44100.0;
constexpr int    kBlock = 512;
constexpr double kTailSeconds = 4.0;

// Set per song before rendering; the playhead and every time conversion read it.
double gBpm  = 128.0;
int    gBars = 64;

inline double beatsToSamples (double beats) { return beats * 60.0 / gBpm * kSr; }

/** A fixed-tempo playhead so the plugin's host-sync paths (arp clock, pump
    clock, tempo-locked delay) run exactly as they would in a DAW. */
struct FixedPlayHead : juce::AudioPlayHead
{
    juce::int64 samplePos = 0;

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo pos;
        pos.setBpm (gBpm);
        pos.setIsPlaying (true);
        pos.setTimeInSamples (samplePos);
        pos.setTimeInSeconds ((double) samplePos / kSr);
        pos.setPpqPosition ((double) samplePos / kSr * gBpm / 60.0);
        return pos;
    }
};

struct Note
{
    double beat;      // absolute, in beats from the top of the track
    int    pitch;
    float  velocity;
    double lengthBeats;
};

struct Part
{
    const char* instrument;
    float       gain;
    std::map<juce::String, float> params;   // applied AFTER the instrument
    std::vector<Note> notes;
};

inline void add (std::vector<Note>& v, double beat, int pitch, float vel, double len)
{
    v.push_back ({ beat, pitch, vel, len });
}

/** bar ranges are half-open: [first, last). */
struct Range { int first, last; };

bool inAny (int bar, std::initializer_list<Range> rs)
{
    for (auto& r : rs) if (bar >= r.first && bar < r.last) return true;
    return false;
}

/** One melodic event: {beat within its bar, midi note, length in beats}. */
struct Phrase { double beat; int note; double len; };

//==============================================================================
//  1. EDM  -  128 BPM, F minor, eight-bar progression
//==============================================================================
// The first cut looped i-VI-III-VII every four bars, which is the most-used
// four bars in recorded music and reads as a stock loop. Eight bars now, and
// bar 8 is a MAJOR V (C) borrowed from harmonic minor - it pulls back to the
// top instead of just starting over.
//   Fm | Db | Ab | Eb | Fm | Db | Bbm | C
const int kEdmBass[8]     = { 41, 37, 44, 39, 41, 37, 46, 36 };
const int kEdmChord[8][3] = { { 65, 68, 72 },   // Fm
                              { 61, 65, 68 },   // Db
                              { 68, 72, 75 },   // Ab
                              { 63, 67, 70 },   // Eb
                              { 65, 68, 72 },   // Fm
                              { 61, 65, 68 },   // Db
                              { 65, 70, 73 },   // Bbm
                              { 64, 67, 72 } }; // C  <- the lift

// A hook with contour and rests. A drop with no melody is a loop with drums on
// it, which is exactly how the previous render came out.
const std::vector<Phrase> kEdmHook[8] = {
    { {0.00,72,0.45},{0.50,68,0.25},{1.00,72,0.45},{2.00,75,0.70},{3.00,72,0.90} },
    { {0.00,77,0.70},{1.00,80,0.45},{2.00,77,0.45},{2.75,75,0.20},{3.00,73,0.90} },
    { {0.00,75,0.45},{0.75,72,0.20},{1.50,75,0.45},{2.00,80,0.90},{3.50,79,0.45} },
    { {0.00,70,0.70},{1.00,75,0.45},{2.00,79,0.70},{3.00,77,0.90} },
    { {0.00,72,0.45},{0.50,68,0.25},{1.00,72,0.45},{2.00,75,0.70},{3.00,72,0.90} },
    { {0.00,77,0.70},{1.00,80,0.45},{2.00,77,0.45},{2.75,75,0.20},{3.00,73,0.90} },
    { {0.00,77,0.45},{0.75,73,0.45},{1.50,70,0.45},{2.00,77,0.90},{3.00,80,0.90} },
    { {0.00,79,0.45},{0.50,76,0.45},{1.00,72,0.45},{2.00,79,1.90} },
};

std::vector<Part> buildEdm()
{
    gBpm = 128.0; gBars = 64;
    const Range intro { 0, 8 }, build { 8, 16 }, drop1 { 16, 32 },
                brk { 32, 40 }, build2 { 40, 44 }, drop2 { 44, 60 }, outro { 60, 64 };

    Part kickLo { "Kick 808",      0.95f, {}, {} };
    Part kickHi { "Kick Punch",    0.70f, {}, {} };
    Part sub    { "Sub 808",       0.80f, { { "pumpAmt", 0.75f }, { "pumpRate", 2.0f } }, {} };
    Part clap   { "Clap",          0.60f, { { "reverb", 0.20f } }, {} };
    Part snare  { "Snare Tight",   0.42f, {}, {} };
    Part hat    { "Hat Closed",    0.32f, {}, {} };
    Part ohat   { "Hat Open",      0.26f, {}, {} };
    Part arp    { "Crystal Pluck", 0.40f,
                  { { "arpMode", 1.0f }, { "arpRate", 3.0f }, { "arpOct", 1.0f },
                    { "arpGate", 0.45f }, { "delay", 0.28f }, { "delaySync", 3.0f },
                    { "reverb", 0.30f } }, {} };
    Part stab   { "Supersaw Lead", 0.36f,
                  { { "pumpAmt", 0.80f }, { "pumpRate", 2.0f }, { "reverb", 0.20f },
                    { "width", 1.0f }, { "release", 70.0f } }, {} };
    // A dirtier layer an octave under the stabs: one saw stack alone is thin,
    // and thin is most of what reads as amateur in a drop.
    Part grit   { "Hoover Bass",   0.24f,
                  { { "pumpAmt", 0.80f }, { "pumpRate", 2.0f },
                    { "drive", 0.30f }, { "width", 0.7f } }, {} };
    Part hook   { "Mainstage",     0.60f,
                  { { "pumpAmt", 0.50f }, { "pumpRate", 2.0f }, { "reverb", 0.26f },
                    { "delay", 0.22f }, { "delaySync", 5.0f }, { "width", 0.85f } }, {} };
    Part pad    { "Vox Ahh",       0.40f, { { "reverb", 0.48f }, { "width", 1.0f } }, {} };
    Part fx     { "Riser Sweep",   0.50f, { { "reverb", 0.35f } }, {} };
    Part crash  { "Crash Splash",  0.42f, {}, {} };

    static const double kStab[] = { 0.00, 0.75, 1.50, 2.25, 3.00, 3.50 };

    for (int bar = 0; bar < gBars; ++bar)
    {
        const double b0 = bar * 4.0;
        const int    ch = bar % 8;
        const bool full = inAny (bar, { drop1, drop2 });
        const bool half = inAny (bar, { build, build2 }) && bar % 8 >= 4;

        if (full || half)
            for (int beat = 0; beat < 4; ++beat)
            {
                add (kickLo.notes, b0 + beat, 36, 1.0f, 0.5);
                add (kickHi.notes, b0 + beat, 36, 0.9f, 0.3);
            }

        if (full || half)
        {
            add (clap.notes, b0 + 1.0, 38, 0.9f, 0.4);
            add (clap.notes, b0 + 3.0, 38, 0.9f, 0.4);
        }

        if (! inAny (bar, { brk, outro }))
            for (int e = 0; e < 8; ++e)
            {
                const double t = b0 + e * 0.5;
                if (e % 2 == 1) add (hat.notes, t, 42, bar < 8 ? 0.55f : 0.9f, 0.2);
                else if (full)  add (hat.notes, t, 42, 0.5f, 0.15);
            }

        if (full && bar % 2 == 1)
            add (ohat.notes, b0 + 3.5, 46, 0.7f, 0.5);

        // Snare pull-ins: eighths, then a rising sixteenth run into the drop.
        if (bar == 14 || bar == 42)
            for (int k = 0; k < 8; ++k)
                add (snare.notes, b0 + k * 0.5, 40, 0.55f, 0.2);
        if (bar == 15 || bar == 43)
        {
            for (int k = 0; k < 4; ++k) add (snare.notes, b0 + k * 0.5, 40, 0.6f, 0.2);
            for (int k = 0; k < 8; ++k)
                add (snare.notes, b0 + 2.0 + k * 0.25, 40, 0.55f + 0.05f * (float) k, 0.15);
        }

        // Bass: offbeat eighths under a downbeat root - the house engine.
        if (full)
        {
            add (sub.notes, b0, kEdmBass[ch], 1.0f, 0.45);
            for (int k = 0; k < 4; ++k)
                add (sub.notes, b0 + k + 0.5, kEdmBass[ch], 0.9f, 0.4);
        }
        else if (inAny (bar, { build, build2 }))
            for (int k = 0; k < 4; ++k)
                add (sub.notes, b0 + k + 0.5, kEdmBass[ch], 0.6f, 0.4);

        if (inAny (bar, { intro, build, build2 }) || (full && ch >= 4))
            for (int n : kEdmChord[ch])
                add (arp.notes, b0, n + 12, bar < 8 ? 0.65f : 0.8f, 3.9);

        if (full)
        {
            for (double t : kStab)
                for (int n : kEdmChord[ch])
                {
                    add (stab.notes, b0 + t, n, 0.85f, 0.40);
                    add (grit.notes, b0 + t, n - 12, 0.75f, 0.40);
                }

            for (const auto& h : kEdmHook[ch])
            {
                add (hook.notes, b0 + h.beat, h.note, 0.95f, h.len);
                if (inAny (bar, { drop2 }))
                    add (hook.notes, b0 + h.beat, h.note + 12, 0.50f, h.len);
            }
        }

        if (inAny (bar, { brk }))
        {
            for (int n : kEdmChord[ch])
                add (pad.notes, b0, n, 0.75f, 3.9);
            if (bar >= 36)
                for (const auto& h : kEdmHook[ch])
                    add (hook.notes, b0 + h.beat, h.note, 0.50f, h.len);
        }
        if (inAny (bar, { build, build2, outro }))
            for (int n : kEdmChord[ch])
                add (pad.notes, b0, n, 0.55f, 3.9);

        if (bar == 15 || bar == 43 || bar == 39)
            add (fx.notes, b0, 60, 0.95f, 4.0);
        if (bar == 16 || bar == 24 || bar == 32 || bar == 44 || bar == 52)
        {
            add (crash.notes, b0, 60, 0.95f, 2.0);
            if (bar == 16 || bar == 44)
                add (sub.notes, b0, kEdmBass[ch] - 12, 1.0f, 1.2);   // impact
        }
    }

    return { kickLo, kickHi, sub, clap, snare, hat, ohat, arp, stab, grit, hook, pad, fx, crash };
}

//==============================================================================
//  2. HIP-HOP  -  85 BPM, C minor, seventh chords, two bars each
//==============================================================================
//   Cm7 | Abmaj7 | Ebmaj7 | Fm7
const int kHopBass[4]     = { 36, 32, 39, 41 };
const int kHopChord[4][4] = { { 60, 63, 67, 70 },   // Cm7
                              { 56, 60, 63, 67 },   // Abmaj7
                              { 63, 67, 70, 74 },   // Ebmaj7
                              { 65, 68, 72, 75 } }; // Fm7

const std::vector<Phrase> kHopLead[8] = {
    { {0.00,67,1.00},{1.50,70,0.45},{2.00,72,1.40} },
    { {0.50,70,0.45},{1.00,67,0.90},{2.50,63,0.90} },
    { {0.00,75,0.90},{1.50,72,0.45},{2.00,68,1.40} },
    { {1.00,72,0.90},{2.50,75,1.00} },
    { {0.00,70,0.90},{1.50,74,0.45},{2.00,79,1.40} },
    { {0.50,77,0.45},{1.00,75,1.40},{3.00,70,0.90} },
    { {0.00,72,0.90},{1.50,68,0.45},{2.00,65,1.40} },
    { {1.00,68,0.90},{2.00,67,1.90} },
};

std::vector<Part> buildHipHop()
{
    gBpm = 85.0; gBars = 40;
    const Range intro { 0, 4 }, hookA { 12, 20 }, brk { 20, 26 }, hookB { 26, 36 };

    Part kick  { "Trap Knock",    1.00f, {}, {} };
    Part snare { "Snare Tight",   0.50f, { { "reverb", 0.12f } }, {} };
    Part snap  { "Finger Snap",   0.30f, {}, {} };
    Part hat   { "Hat Tight",     0.26f, {}, {} };
    Part ohat  { "Hat Trap Open", 0.20f, {}, {} };
    // The 808 SLIDES between roots. That is what the new Glide dial is for,
    // and it is the single most identifiable sound in modern hip-hop.
    Part b808  { "Memphis 808",   0.85f, { { "synthGlide", 110.0f }, { "drive", 0.20f } }, {} };
    Part keys  { "Moody Keys",    0.46f, { { "reverb", 0.34f }, { "delay", 0.16f },
                                           { "delaySync", 3.0f }, { "width", 0.9f } }, {} };
    Part dusty { "Lo-Fi Keys",    0.24f, { { "reverb", 0.28f }, { "drive", 0.18f } }, {} };
    Part lead  { "Trap Flute",    0.44f, { { "reverb", 0.40f }, { "delay", 0.26f },
                                           { "delaySync", 3.0f } }, {} };
    Part bell  { "Cloud Bell",    0.26f, { { "reverb", 0.45f }, { "delay", 0.30f },
                                           { "delaySync", 5.0f } }, {} };

    for (int bar = 0; bar < gBars; ++bar)
    {
        const double b0 = bar * 4.0;
        const int    ch = (bar / 2) % 4;        // two bars per chord
        const int    ph = bar % 8;
        // The break has to STOP, not just get quieter: the first render kept
        // the full kit under it and every bar came out within a decibel of
        // every other one.
        const bool   beat = ! inAny (bar, { intro, brk });
        const bool   full = inAny (bar, { hookA, hookB });

        // Kick: syncopated, not four-on-the-floor. The "and" of one is what
        // makes it walk.
        if (beat)
        {
            add (kick.notes, b0 + 0.00, 36, 1.0f, 0.4);
            add (kick.notes, b0 + 0.75, 36, 0.8f, 0.4);
            add (kick.notes, b0 + 2.50, 36, 0.9f, 0.4);
            if (bar % 4 == 3) add (kick.notes, b0 + 3.50, 36, 0.7f, 0.4);
        }

        if (beat)
        {
            add (snare.notes, b0 + 1.0, 40, 0.9f, 0.35);
            add (snare.notes, b0 + 3.0, 40, 0.9f, 0.35);
            if (full) { add (snap.notes, b0 + 1.0, 40, 0.6f, 0.2);
                        add (snap.notes, b0 + 3.0, 40, 0.6f, 0.2); }
        }

        // Hats: eighths, sixteenth doubles, and a triplet roll every fourth
        // bar - the trap signature.
        if (beat)
        {
            for (int e = 0; e < 8; ++e)
                add (hat.notes, b0 + e * 0.5, 42, e % 2 ? 0.55f : 0.85f, 0.15);
            for (double d : { 1.75, 2.75 })
                add (hat.notes, b0 + d, 42, 0.6f, 0.1);
            if (bar % 4 == 3)
                for (int k = 0; k < 6; ++k)
                    add (hat.notes, b0 + 3.0 + k / 6.0, 42, 0.5f + 0.06f * (float) k, 0.08);
            if (bar % 2 == 1)
                add (ohat.notes, b0 + 2.5, 46, 0.65f, 0.4);
        }

        // 808: a long root that slides in, plus stabs and a walk-up.
        if (beat)
        {
            add (b808.notes, b0 + 0.00, kHopBass[ch], 1.0f, 1.60);
            add (b808.notes, b0 + 2.50, kHopBass[ch], 0.9f, 1.00);
            if (bar % 4 == 3)
                add (b808.notes, b0 + 3.50, kHopBass[(ch + 1) % 4], 0.85f, 0.45);
        }

        for (int n : kHopChord[ch])
        {
            add (keys.notes, b0 + 0.0, n, beat ? 0.75f : 0.6f, 2.4);
            if (beat) add (keys.notes, b0 + 2.75, n, 0.5f, 1.0);
        }
        if (beat)
            for (int n : kHopChord[ch])
                add (dusty.notes, b0 + 1.5, n - 12, 0.5f, 0.8);

        if (full)
            for (const auto& h : kHopLead[ph])
            {
                add (lead.notes, b0 + h.beat, h.note, 0.9f, h.len);
                if (inAny (bar, { hookB }))
                    add (bell.notes, b0 + h.beat, h.note + 12, 0.5f, h.len);
            }
        if (inAny (bar, { brk }))
        {
            for (const auto& h : kHopLead[ph])
                add (bell.notes, b0 + h.beat, h.note, 0.55f, h.len);

            // Last two bars of the break: snare on the backbeat only, walking
            // the kit back in rather than slamming it.
            if (bar >= brk.last - 2)
            {
                add (snare.notes, b0 + 1.0, 40, 0.7f, 0.35);
                add (snare.notes, b0 + 3.0, 40, 0.7f, 0.35);
                for (int e = 0; e < 4; ++e)
                    add (hat.notes, b0 + e, 42, 0.6f, 0.15);
                add (b808.notes, b0, kHopBass[ch], 0.7f, 3.6);
            }
        }
    }

    return { kick, snare, snap, hat, ohat, b808, keys, dusty, lead, bell };
}

//==============================================================================
//  3. EMOTIONAL POP  -  92 BPM, C major, eight bars with a passing G
//==============================================================================
//   C | Em | Am | F | C | G | Am | G
const int kPopBass[8]     = { 36, 40, 33, 41, 36, 43, 33, 43 };
const int kPopChord[8][3] = { { 60, 64, 67 },   // C
                              { 59, 64, 67 },   // Em
                              { 57, 60, 64 },   // Am
                              { 57, 60, 65 },   // F
                              { 60, 64, 67 },   // C
                              { 59, 62, 67 },   // G
                              { 57, 60, 64 },   // Am
                              { 59, 62, 67 } }; // G

const std::vector<Phrase> kPopMel[8] = {
    { {0.00,76,1.00},{1.00,79,1.00},{2.00,76,1.90} },
    { {0.00,74,1.40},{1.50,76,0.45},{2.00,71,1.90} },
    { {0.00,72,1.00},{1.00,76,1.00},{2.00,69,1.90} },
    { {0.00,69,1.00},{1.00,72,1.40},{2.50,77,1.40} },
    { {0.00,76,1.00},{1.00,79,1.00},{2.00,72,1.90} },
    { {0.00,74,1.40},{1.50,71,0.45},{2.00,79,1.90} },
    { {0.00,69,1.00},{1.00,72,1.00},{2.00,76,1.90} },
    { {0.00,74,1.90},{2.00,67,1.90} },
};

std::vector<Part> buildPop()
{
    gBpm = 92.0; gBars = 42;
    const Range intro { 0, 8 }, chorus { 16, 24 },
                verse2 { 24, 32 }, big { 32, 40 }, outro { 40, 42 };

    Part piano  { "Wave Keys",    0.52f, { { "reverb", 0.34f }, { "width", 0.85f } }, {} };
    Part ep     { "Smooth EP",    0.28f, { { "reverb", 0.30f } }, {} };
    Part strings{ "Tape Strings", 0.34f, { { "reverb", 0.50f }, { "width", 1.0f },
                                           { "attack", 320.0f } }, {} };
    Part choir  { "Vox Ahh",      0.30f, { { "reverb", 0.55f }, { "width", 1.0f } }, {} };
    Part mel    { "Felt Mallet",  0.46f, { { "reverb", 0.42f }, { "delay", 0.20f },
                                           { "delaySync", 3.0f } }, {} };
    Part glass  { "Glass Pluck",  0.22f, { { "reverb", 0.50f }, { "delay", 0.26f },
                                           { "delaySync", 5.0f } }, {} };
    Part kick   { "Kick Punch",   0.72f, {}, {} };
    Part snap   { "Finger Snap",  0.42f, { { "reverb", 0.22f } }, {} };
    Part shaker { "Shaker",       0.20f, {}, {} };
    Part bass   { "Analog Warm",  0.55f, { { "drive", 0.10f } }, {} };

    for (int bar = 0; bar < gBars; ++bar)
    {
        const double b0 = bar * 4.0;
        const int    ch = bar % 8;
        const bool   beat = ! inAny (bar, { intro, outro });
        const bool   full = inAny (bar, { chorus, big });

        // Piano: a rolling eighth-note figure over the triad plus its octave.
        {
            const int voice[4] = { kPopChord[ch][0], kPopChord[ch][1],
                                   kPopChord[ch][2], kPopChord[ch][0] + 12 };
            static const int order[8] = { 0, 1, 2, 3, 2, 1, 2, 3 };
            // The chorus plays the figure harder and adds the octave above;
            // with one fixed velocity the verse and the chorus measured the
            // same and the song had no shape.
            const float lift = full ? 1.0f : 0.66f;
            for (int e = 0; e < 8; ++e)
            {
                add (piano.notes, b0 + e * 0.5, voice[order[e]],
                     (e == 0 ? 0.9f : 0.62f) * lift, 0.55);
                if (full && e % 2 == 0)
                    add (piano.notes, b0 + e * 0.5, voice[order[e]] + 12, 0.45f, 0.5);
            }
            for (int n : kPopChord[ch])
                add (ep.notes, b0, n - 12, 0.55f, 3.8);
        }

        // Half-time drums: kick on 1 and 3, the snap only on 3. Anything
        // busier and a ballad stops being a ballad.
        if (beat)
        {
            const float lv = full ? 1.0f : 0.7f;
            add (kick.notes, b0 + 0.0, 36, 0.95f * lv, 0.4);
            add (kick.notes, b0 + 2.0, 36, 0.80f * lv, 0.4);
            add (snap.notes, b0 + 2.0, 40, 0.9f * lv, 0.3);
            if (full)
            {
                add (snap.notes, b0 + 3.5, 40, 0.45f, 0.2);
                for (int e = 0; e < 8; ++e)   // shaker only in the choruses
                    add (shaker.notes, b0 + e * 0.5, 44, e % 2 ? 0.7f : 0.4f, 0.15);
            }

            add (bass.notes, b0 + 0.0, kPopBass[ch], 0.9f * lv, 1.9);
            add (bass.notes, b0 + 2.0, kPopBass[ch], 0.8f * lv, 1.9);
        }

        // Strings are a CHORUS instrument here. Running them under the verses
        // too was most of why the two sections measured the same.
        if (full || inAny (bar, { verse2, outro }))
            for (int n : kPopChord[ch])
                add (strings.notes, b0, n + 12, full ? 0.75f : 0.35f, 3.9);

        if (full)
            for (int n : kPopChord[ch])
                add (choir.notes, b0, n, 0.6f, 3.9);

        if (full)
            for (const auto& h : kPopMel[ch])
            {
                add (mel.notes, b0 + h.beat, h.note, 0.9f, h.len);
                if (inAny (bar, { big }))
                    add (glass.notes, b0 + h.beat, h.note + 12, 0.5f, h.len);
            }
        if (inAny (bar, { verse2 }))
            for (const auto& h : kPopMel[ch])
                add (glass.notes, b0 + h.beat, h.note, 0.45f, h.len);
    }

    return { piano, ep, strings, choir, mel, glass, kick, snap, shaker, bass };
}

//==============================================================================
//  4. BRAZILIAN FUNK  -  130 BPM, A minor, tamborzao
//==============================================================================
// Built for a promo reel, which is why it exists at all: Instagram mutes
// commercial music on accounts that promote a product, so the beat under the
// video has to be one we own. Every sound in it is Slyce.
//
// The rhythm is the tamborzao - the 3-3-2-ish kick against a busy rim/clave
// pattern that every funk mandelao record is built on. Sparse on purpose:
// this style is impact and space, not layers.
//   Am | Am | F | G
const int kFnkBass[4]     = { 33, 33, 29, 31 };
const int kFnkChord[4][3] = { { 69, 72, 76 },   // Am
                              { 69, 72, 76 },   // Am
                              { 65, 69, 72 },   // F
                              { 67, 71, 74 } }; // G

const std::vector<Phrase> kFnkLead[4] = {
    { {0.00,69,0.45},{0.75,72,0.45},{1.50,69,0.45},{2.50,76,0.90} },
    { {0.00,67,0.70},{1.00,69,0.45},{2.00,72,1.40} },
    { {0.00,65,0.45},{0.75,69,0.45},{1.50,72,0.45},{2.50,69,0.90} },
    { {0.00,67,0.45},{1.00,71,0.45},{2.00,74,1.40} },
};

// Sixteenth positions, one bar. The kick is the signature; the rim fills the
// gaps it leaves.
const int kFnkKick[] = { 0, 3, 6, 10, 12 };
const int kFnkRim[]  = { 2, 4, 5, 7, 9, 11, 13, 14 };

std::vector<Part> buildFunk()
{
    gBpm = 130.0; gBars = 48;
    const Range intro { 0, 4 }, main1 { 4, 20 }, brk { 20, 24 },
                main2 { 24, 44 }, outro { 44, 48 };

    Part kick  { "Kick 808",      1.00f, {}, {} };
    Part knock { "Trap Knock",    0.55f, {}, {} };
    Part rim   { "Rim Snap",      0.34f, {}, {} };
    Part clave { "Clave",         0.22f, {}, {} };
    Part clap  { "Clap",          0.55f, { { "reverb", 0.14f } }, {} };
    Part perc  { "Reggaeton Perc",0.28f, {}, {} };
    Part sub   { "Reggaeton Sub", 0.85f, { { "synthGlide", 90.0f }, { "drive", 0.24f } }, {} };
    Part vox   { "Vox Stab",      0.40f, { { "reverb", 0.30f }, { "delay", 0.18f },
                                           { "delaySync", 5.0f } }, {} };
    Part lead  { "Drill Bell Lead", 0.38f, { { "reverb", 0.38f }, { "delay", 0.24f },
                                             { "delaySync", 3.0f } }, {} };
    Part crash { "Crash Splash",  0.36f, {}, {} };
    Part sweep { "Riser Sweep",   0.40f, { { "reverb", 0.35f } }, {} };

    for (int bar = 0; bar < gBars; ++bar)
    {
        const double b0 = bar * 4.0;
        const int    ch = bar % 4;
        const bool   beat = ! inAny (bar, { intro, brk });
        const bool   full = inAny (bar, { main1, main2 });

        if (beat)
        {
            for (int p : kFnkKick)
            {
                add (kick.notes,  b0 + p * 0.25, 36, p == 0 ? 1.0f : 0.9f, 0.35);
                add (knock.notes, b0 + p * 0.25, 36, 0.8f, 0.25);
            }
            for (int p : kFnkRim)
                add (rim.notes, b0 + p * 0.25, 40, (p % 2) ? 0.55f : 0.8f, 0.12);
            for (int p : { 2, 7, 11 })
                add (clave.notes, b0 + p * 0.25, 44, 0.6f, 0.12);

            add (clap.notes, b0 + 1.0, 38, 0.9f, 0.3);
            add (clap.notes, b0 + 3.0, 38, 0.9f, 0.3);
            if (bar % 2 == 1)
                add (perc.notes, b0 + 3.5, 45, 0.7f, 0.3);
        }

        // 808: long root that slides, with a walk-up every fourth bar.
        if (beat)
        {
            add (sub.notes, b0 + 0.0, kFnkBass[ch], 1.0f, 1.40);
            add (sub.notes, b0 + 1.5, kFnkBass[ch], 0.9f, 0.90);
            add (sub.notes, b0 + 2.5, kFnkBass[ch], 0.9f, 1.40);
            if (bar % 4 == 3)
                add (sub.notes, b0 + 3.75, kFnkBass[(ch + 1) % 4], 0.85f, 0.25);
        }

        // Vocal chops on the offbeats - the hook of the style.
        if (full)
            for (double t : { 0.75, 1.75, 2.25, 3.25 })
                add (vox.notes, b0 + t, kFnkChord[ch][0], 0.85f, 0.28);

        if (full || inAny (bar, { brk }))
            for (const auto& h : kFnkLead[ch])
                add (lead.notes, b0 + h.beat, h.note,
                     inAny (bar, { brk }) ? 0.6f : 0.9f, h.len);

        if (inAny (bar, { intro, brk }))
            for (int n : kFnkChord[ch])
                add (vox.notes, b0, n, 0.5f, 3.8);

        if (bar == 3 || bar == 23 || bar == 43)
            add (sweep.notes, b0, 60, 0.9f, 4.0);
        if (bar == 4 || bar == 20 || bar == 24 || bar == 36)
            add (crash.notes, b0, 60, 0.9f, 2.0);
    }

    return { kick, knock, rim, clave, clap, perc, sub, vox, lead, crash, sweep };
}

//==============================================================================
//  SHOWCASE  -  the physically modelled voices, one after another
//==============================================================================
// Not a track. Judging whether the string model and the body resonators were
// worth having means hearing them, and hunting for twelve instruments inside a
// menu of 376 is how that does not happen. Each gets four bars of a phrase
// that suits its mechanism: struck things get repeated strikes so the decay is
// audible, plucked things get a run, sustained things get held chords.
struct Showpiece
{
    const char* instrument;
    const char* kind;          // "pluck" | "strike" | "hold"
    int root;
};

const Showpiece kShow[] = {
    { "Syn Harp",       "pluck",  60 },
    { "Syn Koto",       "pluck",  57 },
    { "Syn Sitar",      "pluck",  57 },
    { "Syn Dulcimer",   "pluck",  62 },
    { "Syn 12-String",  "pluck",  55 },
    { "Syn Harpsi",     "pluck",  60 },
    { "Syn Kalimba",    "strike", 65 },
    { "Syn Marimba",    "strike", 60 },
    { "Syn Music Box",  "strike", 72 },
    { "Syn Steel Pan",  "strike", 60 },
    { "Cloud Bell",     "strike", 67 },
    { "Syn Grand",      "hold",   53 },
    { "Smooth EP",      "hold",   53 },
    { "Pluck Bass",     "pluck",  41 },
};

std::vector<Part> buildShowcase()
{
    gBpm = 96.0;
    const int perPiece = 4;                       // bars each
    const int count = (int) (sizeof (kShow) / sizeof (kShow[0]));
    gBars = count * perPiece;

    std::vector<Part> parts;
    // A quiet click so the ear has a grid to hear the decays against.
    Part click { "Rim Snap", 0.16f, {}, {} };

    for (int i = 0; i < count; ++i)
    {
        const auto& sp = kShow[i];
        Part p { sp.instrument, 0.85f,
                 { { "reverb", 0.22f }, { "width", 0.9f } }, {} };
        const double b0 = i * perPiece * 4.0;
        const juce::String kind (sp.kind);

        if (kind == "pluck")
        {
            // A run up and back: every note is a fresh pluck, so the strike
            // and the decay are both exposed.
            static const int steps[] = { 0, 4, 7, 12, 7, 4, 0, 7, 12, 16, 12, 7 };
            for (int bar = 0; bar < perPiece; ++bar)
                for (int k = 0; k < 12; ++k)
                    add (p.notes, b0 + bar * 4.0 + k * (4.0 / 12.0),
                         sp.root + steps[k], k % 3 == 0 ? 1.0f : 0.8f, 0.30);
        }
        else if (kind == "strike")
        {
            // Repeated strikes on one note, then a chord: if the body is doing
            // anything, the ring between hits is where it shows.
            for (int bar = 0; bar < perPiece; ++bar)
            {
                for (double t : { 0.0, 0.75, 1.5, 2.25 })
                    add (p.notes, b0 + bar * 4.0 + t, sp.root, 1.0f, 0.5);
                for (int n : { 0, 4, 7 })
                    add (p.notes, b0 + bar * 4.0 + 3.0, sp.root + n, 0.9f, 1.0);
            }
        }
        else
        {
            for (int bar = 0; bar < perPiece; ++bar)
                for (int n : { 0, 7, 12, 16 })
                    add (p.notes, b0 + bar * 4.0, sp.root + n, 0.85f, 3.6);
        }

        for (int bar = 0; bar < perPiece; ++bar)
            add (click.notes, b0 + bar * 4.0, 40, 0.5f, 0.1);

        parts.push_back (p);
    }
    parts.push_back (click);
    return parts;
}

//==============================================================================
void setParam (VocalChopAudioProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.getAPVTS().getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    else
        printf ("  !! unknown parameter '%s'\n", id.toRawUTF8());
}

void renderPart (const Part& part, juce::AudioBuffer<float>& out)
{
    const auto names = VocalChopAudioProcessor::getInstrumentNames();
    const int  idx   = names.indexOf (part.instrument);
    if (idx < 0) { printf ("  !! missing instrument '%s'\n", part.instrument); return; }

    VocalChopAudioProcessor proc;
    FixedPlayHead playHead;
    proc.setPlayHead (&playHead);
    proc.prepareToPlay (kSr, kBlock);
    proc.applyInstrument (idx);
    for (const auto& kv : part.params)
        setParam (proc, kv.first, kv.second);

    juce::MidiBuffer all;
    for (const auto& n : part.notes)
    {
        const int on  = (int) beatsToSamples (n.beat);
        const int off = (int) beatsToSamples (n.beat + n.lengthBeats);
        all.addEvent (juce::MidiMessage::noteOn  (1, n.pitch, n.velocity), on);
        all.addEvent (juce::MidiMessage::noteOff (1, n.pitch), juce::jmax (on + 1, off));
    }

    juce::AudioBuffer<float> block (2, kBlock);
    const int total = out.getNumSamples();
    for (int pos = 0; pos < total; pos += kBlock)
    {
        const int n = juce::jmin (kBlock, total - pos);
        block.setSize (2, n, false, false, true);
        block.clear();

        juce::MidiBuffer slice;
        for (const auto meta : all)
            if (meta.samplePosition >= pos && meta.samplePosition < pos + n)
                slice.addEvent (meta.getMessage(), meta.samplePosition - pos);

        playHead.samplePos = pos;
        proc.processBlock (block, slice);

        for (int ch = 0; ch < 2; ++ch)
            out.addFrom (ch, pos, block, ch, 0, n, part.gain);
    }
}
} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::File outFile (argc > 1 ? juce::String (argv[1])
                                       : juce::String ("slyce-demo.wav"));
    const juce::String song (argc > 2 ? juce::String (argv[2]) : juce::String ("edm"));
    const juce::String solo (argc > 3 ? juce::String (argv[3]) : juce::String());

    std::vector<Part> parts;
    float push = 7.0f;                       // pre-limiter drive, per genre
    if      (song == "hiphop") { parts = buildHipHop(); push = 5.0f; }
    else if (song == "pop")    { parts = buildPop();    push = 4.0f; }
    else if (song == "funk")   { parts = buildFunk();   push = 6.0f; }
    else if (song == "showcase") { parts = buildShowcase(); push = 2.0f; }
    else                       { parts = buildEdm();    push = 7.0f; }

    printf ("song %s  %.0f BPM  %d bars\n", song.toRawUTF8(), gBpm, gBars);

    const int total = (int) beatsToSamples (gBars * 4.0) + (int) (kTailSeconds * kSr);
    juce::AudioBuffer<float> mix (2, total);
    mix.clear();

    for (const auto& part : parts)
    {
        if (solo.isNotEmpty() && solo != part.instrument)
            continue;
        printf ("  %-16s %4d notes\n", part.instrument, (int) part.notes.size());
        fflush (stdout);
        renderPart (part, mix);
    }

    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        peak = juce::jmax (peak, mix.getMagnitude (ch, 0, total));
    printf ("mix peak %.3f\n", peak);
    if (peak > 0.0001f)
        mix.applyGain (1.0f / peak);

    // Push into the plugin's own look-ahead limiter for release loudness. A
    // ballad gets less push than a festival drop, on purpose.
    if (solo.isEmpty())
    {
        mix.applyGain (juce::Decibels::decibelsToGain (push));

        Limiter master;
        master.prepare (kSr, kBlock);
        master.setCeiling (0.891f);          // -1 dBFS
        for (int pos = 0; pos < total; pos += kBlock)
        {
            const int n = juce::jmin (kBlock, total - pos);
            juce::AudioBuffer<float> view (mix.getArrayOfWritePointers(), 2, pos, n);
            master.process (view);
        }
    }

    mix.applyGainRamp (total - (int) (2.0 * kSr), (int) (2.0 * kSr), 1.0f, 0.0f);

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (outFile.createOutputStream());
    if (stream == nullptr) { printf ("cannot write file\n"); return 1; }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.release(), kSr, 2, 24, {}, 0));
    if (writer == nullptr) { printf ("cannot create writer\n"); return 1; }

    writer->writeFromAudioSampleBuffer (mix, 0, total);
    writer.reset();

    printf ("wrote %s (%.1f s)\n", outFile.getFullPathName().toRawUTF8(),
            (double) total / kSr);
    return 0;
}
