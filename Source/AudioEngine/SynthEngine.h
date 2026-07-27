#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

/**
    Polyphonic synth engine v3 — the "Synth" engine mode.

    Per voice:
      - up to 7 unison oscillators (PolyBLEP saw/square, sine, triangle) with
        symmetric detune spread and equal-power stereo panning
      - sine sub-oscillator, white-noise layer, optional 2-op FM/PM pair
      - vibrato LFO plus per-voice analog drift (a slow random walk in cents,
        so stacked voices shimmer like real analog oscillators)
      - resonant TPT state-variable lowpass with its own decay envelope and
        velocity-to-cutoff modulation (soft hits play darker)
      - curved ADSR (ease-out attack, exponential-feel decay/release) — linear
        ramps sound cheap; these bloom and die away naturally

    Synth bus (applied to the summed synth signal only, before it joins the
    host buffer):
      - gentle tanh saturation glue
      - stereo chorus (opposite-phase modulated delays L/R) for pads, keys and
        supersaws — the patch decides how much

    Live parameters (wave/detune/octave/ADSR) come from APVTS every block;
    Patch settings are atomics written on the message thread per instrument.
*/
class SynthEngine
{
public:
    enum Wave { Saw = 0, Square, Sine, Triangle };

    static constexpr int kMaxUnison = 7;

    struct Patch
    {
        std::atomic<int>   unison        { 1 };
        std::atomic<float> stereoSpread  { 0.5f };
        std::atomic<float> subLevel      { 0.0f };
        std::atomic<float> noiseLevel    { 0.0f };
        std::atomic<float> fmAmount      { 0.0f };
        std::atomic<float> fmRatio       { 2.0f };
        std::atomic<float> vibRateHz     { 0.0f };
        std::atomic<float> vibDepthCents { 0.0f };
        std::atomic<float> filterCutoff  { 20000.0f };
        std::atomic<float> filterEnvOct  { 0.0f };
        std::atomic<float> filterEnvMs   { 200.0f };
        // v3 lushness controls -------------------------------------------------
        std::atomic<float> filterQ       { 0.71f };  // resonance (0.5..8)
        std::atomic<float> driftCents    { 2.5f };   // analog drift depth
        std::atomic<float> velToFilterOct{ 0.8f };   // velocity -> cutoff
        std::atomic<float> chorusMix     { 0.0f };   // 0..1 bus chorus
        std::atomic<float> satAmount     { 0.15f };  // 0..1 bus saturation
        std::atomic<float> lfoRateHz     { 2.0f };   // filter LFO rate
        std::atomic<float> lfoDepthOct   { 0.0f };   // filter LFO depth (octaves)
        std::atomic<float> pitchEnvOct   { 0.0f };   // pitch drop (octaves, drums)
        std::atomic<float> pitchEnvMs    { 60.0f };  // pitch envelope decay
        std::atomic<float> glideMs       { 0.0f };   // portamento from the last note
        // --- what separates a synth from an instrument ------------------------
        // Real instruments do not start cleanly. A hammer, a pick, a mallet or
        // a breath makes a burst of inharmonic noise BEFORE the tone arrives,
        // and the ear uses it to decide whether it is hearing an instrument or
        // an oscillator. Every voice here started as a clean ramp, which is
        // most of why 376 sounds all read as "synth".
        std::atomic<float> attackNoise    { 0.0f };   // 0..1 burst level
        std::atomic<float> attackTone     { 3.0f };   // burst centre, x the note
        std::atomic<float> attackMs       { 14.0f };  // burst decay
        // Strings and bars are INHARMONIC: their partials stretch sharp of the
        // harmonic series. Perfectly harmonic partials are why synthesised
        // pianos and bells sound flat and glassy.
        std::atomic<float> inharmonic     { 0.0f };   // 0..1 stretch amount

        // STAGE 3 — a plucked/struck string, modelled rather than imitated.
        // A delay line the length of one period, fed back through a damping
        // filter: the wave really does travel down the string and reflect.
        // No oscillator shape reproduces the way that decays.
        std::atomic<float> stringMix      { 0.0f };   // 0 = oscillator, 1 = string
        std::atomic<float> stringDamp     { 0.55f };  // 0 dull .. 1 bright
        std::atomic<float> stringDecay    { 0.6f };   // 0 short .. 1 long

        // STAGE 4 — the waveform itself moves through the note, instead of
        // the filter being the only thing that changes.
        std::atomic<float> morphAmt       { 0.0f };   // 0..1 depth
        std::atomic<float> morphMs        { 400.0f }; // time constant
        std::atomic<int>   morphTo        { 2 };      // wave to arrive at

        // STAGE 2/5 — the body. One instrument, many strings: the resonator
        // bank lives on the BUS, not per voice, because that is physically
        // what a soundboard is. Long decays give sympathetic ring.
        std::atomic<int>   bodyType       { -1 };     // -1 off, else family id
        std::atomic<float> bodyAmount     { 0.0f };   // 0..1 wet
        // Vocal formant bank: -1 = off, 0..4 = A E I O U vowel resonances.
        std::atomic<int>   formantVowel  { -1 };
        std::atomic<float> formantAmount { 0.0f };   // 0..1 dry/wet

        void resetToInit()
        {
            unison = 1; stereoSpread = 0.5f; subLevel = 0.0f; noiseLevel = 0.0f;
            fmAmount = 0.0f; fmRatio = 2.0f; vibRateHz = 0.0f; vibDepthCents = 0.0f;
            filterCutoff = 20000.0f; filterEnvOct = 0.0f; filterEnvMs = 200.0f;
            filterQ = 0.71f; driftCents = 2.5f; velToFilterOct = 0.8f;
            chorusMix = 0.0f; satAmount = 0.15f;
            lfoRateHz = 2.0f; lfoDepthOct = 0.0f;
            pitchEnvOct = 0.0f; pitchEnvMs = 60.0f;
            glideMs = 0.0f;
            attackNoise = 0.0f; attackTone = 3.0f; attackMs = 14.0f;
            inharmonic = 0.0f;
            stringMix = 0.0f; stringDamp = 0.55f; stringDecay = 0.6f;
            morphAmt = 0.0f; morphMs = 400.0f; morphTo = 2;
            bodyType = -1; bodyAmount = 0.0f;
            formantVowel = -1; formantAmount = 0.0f;
        }
    };

    void prepare (juce::dsp::ProcessSpec spec);
    void reset();

    void setEnvelope (float attackMs, float decayMs, float sustain0to1, float releaseMs);
    void setWave (int waveType)      { wave = juce::jlimit (0, 3, waveType); }
    void setDetuneCents (float c)    { detuneCents = juce::jlimit (0.0f, 50.0f, c); }
    void setOctave (int oct)         { octave = juce::jlimit (-2, 2, oct); }

    Patch& patch() { return patchSettings; }

    /** pitchNoteOverride >= 0 plays THAT pitch while the voice stays keyed to
        midiNote for note-off matching (drum-kit mode: fixed drum pitches). */
    void noteOn  (int midiNote, float velocity, int pitchNoteOverride = -1);
    void noteOff (int midiNote);
    void tapNote (int midiNote, float velocity, int pitchNoteOverride = -1);
    void releaseAll();

    void render (juce::AudioBuffer<float>& out, int numSamples);

private:
    struct Voice
    {
        enum class Stage { attack, decay, sustain, release, idle };

        int    note = -1;
        float  velocity = 0.0f;

        double phases[kMaxUnison] {};
        double incs[kMaxUnison] {};
        float  panL[kMaxUnison] {}, panR[kMaxUnison] {};
        int    unison = 1;
        float  unisonNorm = 1.0f;

        double subPhase = 0.0,  subInc = 0.0;

        // Pitch envelope (percussion "drop"): 1 -> 0 exponential.
        float penv = 0.0f, penvCoeff = 0.0f, penvOct = 0.0f;

        // Portamento: the note starts at the PREVIOUS note's pitch and slides
        // to its own. Held as a frequency ratio that decays toward 1.
        float glideRatio = 1.0f, glideCoeff = 0.0f;

        // Attack transient: a band-passed noise burst that decays in a few
        // milliseconds, plus the state of the resonator that colours it.
        // x1/x2 are the INPUT history and z1/z2 the OUTPUT history - a biquad
        // needs both, and conflating them turns the band-pass into a broadband
        // two-pole that leaks raw noise into every note.
        float atkLevel = 0.0f, atkCoeff = 0.0f, atkAmt = 0.0f;
        float atkB0 = 0.0f, atkB2 = 0.0f, atkA1 = 0.0f, atkA2 = 0.0f;
        float atkX1 = 0.0f, atkX2 = 0.0f, atkZ1 = 0.0f, atkZ2 = 0.0f;

        // Inharmonic partial: a second bank running sharp of the fundamental,
        // which is what a struck string or a metal bar actually does. It gets
        // its OWN decay because a stiff string's overtones die before its
        // fundamental does - held flat, it just reads as an octave layer.
        double inhPhase = 0.0, inhInc = 0.0;
        float  inhLevel = 0.0f, inhCoeff = 1.0f;

        // Karplus-Strong string: a circular buffer one period long, read with
        // fractional interpolation so the pitch is exact rather than quantised
        // to whole samples.
        std::vector<float> ksBuf;
        float  ksPos = 0.0f, ksDelay = 0.0f, ksFb = 0.995f;
        float  ksLast = 0.0f, ksDamp = 0.5f, ksMix = 0.0f, ksMakeup = 1.0f;
        int    ksExcite = 0;

        // Wavetable morph: the shape the note is heading toward, and where it
        // currently is between the two.
        float  morphPos = 0.0f, morphCoeff = 0.0f, morphDepth = 0.0f;
        int    morphTarget = 2;
        double fmCarPhase = 0.0, fmCarInc = 0.0;
        double fmModPhase = 0.0, fmModInc = 0.0;
        double vibPhase = 0.0,  vibInc = 0.0;

        float subLevel = 0.0f, noiseLevel = 0.0f, fmAmount = 0.0f;
        float vibDepthCents = 0.0f;

        // Analog drift: slow random walk in cents.
        float driftCents = 0.0f;     // current value
        float driftDepth = 0.0f;     // max cents

        // Resonant TPT SVF (lowpass), per channel states.
        float fltBaseHz = 20000.0f, fltEnvOct = 0.0f, fltK = 1.4f;
        float fenv = 0.0f, fenvCoeff = 1.0f;
        float svfA1 = 0, svfA2 = 0, svfA3 = 0;
        float ic1L = 0, ic2L = 0, ic1R = 0, ic2R = 0;
        int   fltUpdateCounter = 0;
        bool  fltActive = false;

        // Curved ADSR.
        Stage  stage = Stage::idle;
        int    stagePos = 0;
        float  releaseFrom = 1.0f;
        int    attackSamples = 1, decaySamples = 1, releaseSamples = 1;
        float  sustainLevel = 1.0f;

        int    autoOffCounter = -1;
        int    waveSnap = Saw;   // wave captured at note-start: kit mode swaps
                                 // the engine wave per HIT, so a ringing drum
                                 // must keep the shape it was born with

        bool  isActive() const { return stage != Stage::idle; }
        float envelope() const;
    };

    float  renderOsc (double phase, double inc, int waveType) const;
    Voice* findFreeVoice();
    void   startVoice (Voice&, int midiNote, float velocity, int autoOffSamples,
                       int pitchNoteOverride = -1);
    void   processBus (int numSamples);   // saturation + chorus on the scratch

    static constexpr int kMaxVoices = 16;
    static constexpr int kChorusSize = 8192;

    std::array<Voice, kMaxVoices> voices;

    Patch  patchSettings;
    // Where the next note glides FROM (-1 = no previous note).
    int    lastStartedNote = -1;
    juce::Random noiseRng;
    double lfoPhaseBase = 0.0;   // global filter-LFO phase (0..1)

    // Synth-only scratch bus so chorus/saturation never touch the chop signal.
    juce::AudioBuffer<float> scratch;

    /** The instrument's BODY. Six tuned resonators, struck by whatever the
        voices produce and left to ring — a soundboard, a shell, a tube. One
        bank for the whole engine, because an instrument has one body however
        many strings are on it, and the long decays are what make a struck
        note bloom instead of stopping dead. */
    struct BodyMode { float f, q, gain; float z1L, z2L, z1R, z2R, b0, a1, a2; };
    std::array<BodyMode, 6> bodyModes {};
    int   bodyTypeSet = -2;
    void  updateBodyBank (int type);

    // Chorus state.
    std::vector<float> chorusLine[2];
    int    chorusWrite = 0;
    double chorusLfo = 0.0;

    // Formant bank: three parallel bandpass biquads per channel, tuned to a
    // vowel's F1/F2/F3. This is what makes VOCAL patches read as a VOICE.
    struct FormantBand { float b0=0, b1=0, b2=0, a1=0, a2=0,
                               x1L=0, x2L=0, y1L=0, y2L=0,
                               x1R=0, x2R=0, y1R=0, y2R=0, gain=0; };
    FormantBand formantBands[3];
    int  formantVowelSet = -2;   // last vowel coefficients were built for
    void updateFormantBank (int vowel);

    double sampleRate = 44100.0;
    int    wave = Saw;
    int    octave = 0;
    float  detuneCents = 7.0f;

    float attackMs = 5.0f, decayMs = 120.0f, sustainLvl = 0.75f, releaseMs = 60.0f;
};
