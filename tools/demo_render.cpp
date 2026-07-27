// Offline arrangement renderer: builds the EDM demo track for the product
// page by running one plugin instance per part and summing them.
//
// Everything here is real plugin output - the same synth engine, arpeggiator,
// sidechain pump and limiter the buyer gets. Nothing is sweetened afterwards
// beyond a single gain stage.
//
//   SlyceDemoRender <out.wav>
#include "PluginProcessor.h"
#include "DSP/Limiter.h"
#include <cstdio>
#include <map>
#include <vector>

namespace
{
constexpr double kBpm   = 128.0;
constexpr double kSr    = 44100.0;
constexpr int    kBlock = 512;
constexpr int    kBars  = 24;
constexpr double kTailSeconds = 3.0;   // room for the last reverb/delay tail

inline double beatsToSamples (double beats) { return beats * 60.0 / kBpm * kSr; }

/** A fixed-tempo playhead so the plugin's host-sync paths (arp clock, pump
    clock, tempo-locked delay) run exactly as they would in a DAW. */
struct FixedPlayHead : juce::AudioPlayHead
{
    juce::int64 samplePos = 0;

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo pos;
        pos.setBpm (kBpm);
        pos.setIsPlaying (true);
        pos.setTimeInSamples (samplePos);
        pos.setTimeInSeconds ((double) samplePos / kSr);
        pos.setPpqPosition ((double) samplePos / kSr * kBpm / 60.0);
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

//==============================================================================
// F minor: i - VI - III - VII, one chord per bar.
const int kBassRoot[4] = { 41, 37, 44, 39 };                 // F2 Db2 Ab2 Eb2
const int kChord[4][3] = { { 65, 68, 72 },                   // Fm
                           { 61, 65, 68 },                   // Db
                           { 68, 72, 75 },                   // Ab
                           { 63, 67, 70 } };                 // Eb

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

std::vector<Part> buildArrangement()
{
    // Section map (bars):
    //   0-3   intro   filtered pluck arp + offbeat hats
    //   4-7   build   + offbeat bass, claps, vox bed, snare roll + riser in 7
    //   8-15  DROP    hook melody over stabbed chords, four-on-the-floor
    //   16-19 break   pad + vox, drums out, riser back in
    //   20-23 DROP 2  the drop with the hook doubled an octave up
    //
    // The FIRST version of this held one four-beat chord per bar under the
    // drums and read as background music, not as EDM. A drop needs a HOOK
    // and it needs RHYTHM: the chords are stabs on a syncopated eighth
    // pattern now, a lead plays an actual melody over them, the bass sits on
    // the offbeats, and a snare roll pulls into the drop.
    const Range intro { 0, 4 }, build { 4, 8 }, drop1 { 8, 16 },
                brk { 16, 20 }, drop2 { 20, 24 };

    // Two kicks layered: one for the body, one for the click. A single synth
    // kick is thin, and thin kick = "this is not a dance record".
    Part kickLo { "Kick 808",       0.95f, {}, {} };
    Part kickHi { "Kick Punch",     0.75f, {}, {} };
    Part sub    { "Sub 808",        0.80f, { { "pumpAmt", 0.75f }, { "pumpRate", 2.0f } }, {} };
    Part clap   { "Clap",           0.60f, { { "reverb", 0.18f } }, {} };
    Part snare  { "Snare Tight",    0.45f, {}, {} };
    Part hat    { "Hat Closed",     0.34f, {}, {} };
    Part ohat   { "Hat Open",       0.26f, {}, {} };
    Part arp    { "Crystal Pluck",  0.42f,
                  { { "arpMode", 1.0f }, { "arpRate", 3.0f }, { "arpOct", 1.0f },
                    { "arpGate", 0.45f }, { "delay", 0.28f }, { "delaySync", 3.0f },
                    { "reverb", 0.30f } }, {} };
    Part stab   { "Supersaw Lead",  0.40f,
                  { { "pumpAmt", 0.80f }, { "pumpRate", 2.0f },
                    { "reverb", 0.22f }, { "width", 1.0f }, { "release", 90.0f } }, {} };
    Part hook   { "Mainstage",      0.62f,
                  { { "pumpAmt", 0.55f }, { "pumpRate", 2.0f },
                    { "reverb", 0.30f }, { "delay", 0.20f }, { "delaySync", 5.0f },
                    { "width", 0.85f } }, {} };
    Part pad    { "Vox Ahh",        0.40f, { { "reverb", 0.45f }, { "width", 1.0f } }, {} };
    Part fx     { "Riser Sweep",    0.50f, { { "reverb", 0.35f } }, {} };
    Part crash  { "Crash Splash",   0.45f, {}, {} };

    // The hook, one line per chord of the loop: {beat within the bar, note}.
    // Written out rather than generated - a melody is the one thing that has
    // to be composed, not derived.
    struct HookNote { double beat; int note; double len; };
    static const std::vector<HookNote> kHook[4] = {
        { {0.00, 72, 0.70}, {0.75, 68, 0.70}, {1.50, 72, 0.45},
          {2.00, 75, 0.90}, {3.00, 72, 0.90} },                       // Fm
        { {0.00, 77, 0.90}, {1.00, 75, 0.70}, {1.75, 73, 0.70},
          {2.50, 77, 0.90}, {3.50, 80, 0.45} },                       // Db
        { {0.00, 75, 0.70}, {0.75, 72, 0.70}, {1.50, 75, 0.45},
          {2.00, 68, 0.90}, {3.00, 72, 0.90} },                       // Ab
        { {0.00, 70, 0.90}, {1.00, 72, 0.70}, {1.75, 75, 0.70},
          {2.50, 79, 0.65}, {3.25, 77, 0.70} },                       // Eb
    };

    // Syncopated stab pattern - the "and" hits are what make it move.
    static const double kStab[] = { 0.00, 0.75, 1.50, 2.25, 3.00, 3.50 };

    for (int bar = 0; bar < kBars; ++bar)
    {
        const double b0 = bar * 4.0;
        const int    ch = bar % 4;

        const bool fullKit = inAny (bar, { drop1, drop2 });
        const bool halfKit = (bar == 6 || bar == 7);

        // --- drums ------------------------------------------------------
        if (fullKit || halfKit)
            for (int beat = 0; beat < 4; ++beat)
            {
                add (kickLo.notes, b0 + beat, 36, 1.0f, 0.5);
                add (kickHi.notes, b0 + beat, 36, 0.9f, 0.3);
            }

        if (fullKit || halfKit)
        {
            add (clap.notes, b0 + 1.0, 38, 0.9f, 0.4);
            add (clap.notes, b0 + 3.0, 38, 0.9f, 0.4);
        }

        if (inAny (bar, { intro, build, drop1, drop2 }))
            for (int eighth = 0; eighth < 8; ++eighth)
            {
                const double t = b0 + eighth * 0.5;
                if (eighth % 2 == 1)
                    add (hat.notes, t, 42, bar < 4 ? 0.6f : 0.9f, 0.2);
                else if (fullKit)
                    add (hat.notes, t, 42, 0.5f, 0.15);
            }

        if (fullKit && bar % 2 == 1)
            add (ohat.notes, b0 + 3.5, 46, 0.7f, 0.5);

        // Snare roll: quarters, then eighths, then a 16th run into the drop.
        if (bar == 6)
            for (int k = 0; k < 4; ++k)
                add (snare.notes, b0 + k, 40, 0.55f, 0.25);
        if (bar == 7)
        {
            for (int k = 0; k < 4; ++k)                       // eighths
                add (snare.notes, b0 + k * 0.5, 40, 0.6f, 0.2);
            for (int k = 0; k < 8; ++k)                       // 16ths, rising
                add (snare.notes, b0 + 2.0 + k * 0.25, 40,
                     0.55f + 0.05f * (float) k, 0.15);
        }

        // --- bass: offbeat eighths, the engine of a house drop ------------
        if (fullKit)
        {
            add (sub.notes, b0, kBassRoot[ch], 1.0f, 0.45);
            for (int k = 0; k < 4; ++k)
                add (sub.notes, b0 + k + 0.5, kBassRoot[ch], 0.9f, 0.4);
        }
        else if (inAny (bar, { build }))
            for (int k = 0; k < 4; ++k)
                add (sub.notes, b0 + k + 0.5, kBassRoot[ch], 0.6f, 0.4);

        // --- arp ----------------------------------------------------------
        if (inAny (bar, { intro, build }) || (fullKit && bar % 4 >= 2))
            for (int n : kChord[ch])
                add (arp.notes, b0, n + 12, bar < 4 ? 0.7f : 0.8f, 3.9);

        // --- drop: stabbed chords + the hook on top ------------------------
        if (fullKit)
        {
            for (double t : kStab)
                for (int n : kChord[ch])
                    add (stab.notes, b0 + t, n, 0.85f, 0.42);

            for (const auto& h : kHook[ch])
            {
                add (hook.notes, b0 + h.beat, h.note, 0.95f, h.len);
                if (inAny (bar, { drop2 }))          // second drop: octave up
                    add (hook.notes, b0 + h.beat, h.note + 12, 0.55f, h.len);
            }
        }

        // --- break: the hook again, soft, over the vocal bed ---------------
        if (inAny (bar, { brk }))
        {
            for (int n : kChord[ch])
                add (pad.notes, b0, n, 0.75f, 3.9);
            if (bar >= 18)
                for (const auto& h : kHook[ch])
                    add (hook.notes, b0 + h.beat, h.note, 0.55f, h.len);
        }
        if (bar == 6 || bar == 7)
            for (int n : kChord[ch])
                add (pad.notes, b0, n, 0.7f, 3.9);

        // --- transitions ---------------------------------------------------
        if (bar == 7 || bar == 19)
            add (fx.notes, b0, 60, 0.95f, 4.0);
        if (bar == 8 || bar == 16 || bar == 20)
        {
            add (crash.notes, b0, 60, 0.95f, 2.0);
            add (sub.notes,   b0, kBassRoot[ch] - 12, 1.0f, 1.0);   // impact
        }
    }

    return { kickLo, kickHi, sub, clap, snare, hat, ohat, arp, stab, hook, pad, fx, crash };
}

//==============================================================================
void setParam (VocalChopAudioProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.getAPVTS().getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    else
        printf ("  !! unknown parameter '%s'\n", id.toRawUTF8());
}

/** Renders one part into `out` (which is assumed cleared and large enough). */
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

    // Flatten the part into one absolute-sample MIDI list.
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

    // Optional second argument solos one part by instrument name - the only
    // practical way to hear/measure a single layer of the arrangement.
    const juce::String solo (argc > 2 ? juce::String (argv[2]) : juce::String());

    const int total = (int) beatsToSamples (kBars * 4.0) + (int) (kTailSeconds * kSr);
    juce::AudioBuffer<float> mix (2, total);
    mix.clear();

    for (const auto& part : buildArrangement())
    {
        if (solo.isNotEmpty() && solo != part.instrument)
            continue;
        printf ("rendering %-16s (%d notes)\n", part.instrument, (int) part.notes.size());
        fflush (stdout);
        renderPart (part, mix);
    }

    // Master: normalise, push into the plugin's own look-ahead limiter for
    // release-level loudness, then trim to -1 dBFS. Peak-normalising alone
    // left the track at -17 dBFS RMS, which reads as "quiet and amateur"
    // next to anything else on a store page.
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        peak = juce::jmax (peak, mix.getMagnitude (ch, 0, total));
    printf ("mix peak %.3f\n", peak);
    if (peak > 0.0001f)
        mix.applyGain (1.0f / peak);

    if (solo.isEmpty())
    {
        mix.applyGain (juce::Decibels::decibelsToGain (7.0f));

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

    // Fade the tail so the reverb does not stop dead.
    mix.applyGainRamp (total - (int) (1.5 * kSr), (int) (1.5 * kSr), 1.0f, 0.0f);

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (outFile.createOutputStream());
    if (stream == nullptr) { printf ("cannot write %s\n", outFile.getFullPathName().toRawUTF8()); return 1; }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.release(), kSr, 2, 24, {}, 0));
    if (writer == nullptr) { printf ("cannot create writer\n"); return 1; }

    writer->writeFromAudioSampleBuffer (mix, 0, total);
    writer.reset();

    printf ("wrote %s (%.1f s)\n", outFile.getFullPathName().toRawUTF8(),
            (double) total / kSr);
    return 0;
}
