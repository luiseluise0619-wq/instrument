#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

/**
    Polyphonic synth engine v2 — the "Synth" engine mode.

    Architecture per voice:
      - up to 7 unison oscillators (PolyBLEP saw/square, sine, triangle) with
        symmetric detune spread and stereo panning (supersaws, reese basses)
      - sine sub-oscillator one octave down
      - white-noise layer (breath / attack air)
      - optional 2-op FM/PM pair (bells, electric pianos), blended in
      - vibrato LFO (cents depth)
      - per-voice 2-pole lowpass with its own decay envelope (plucks, basses)
      - ADSR amp envelope; tapNote() auto-releases for on-screen key clicks

    Live parameters (wave/detune/octave/ADSR) come from APVTS every block.
    The extended Patch settings are what make each Instrument preset sound
    genuinely different; they're plain atomics written from the message
    thread when an instrument is applied and read on the audio thread.
*/
class SynthEngine
{
public:
    enum Wave { Saw = 0, Square, Sine, Triangle };

    static constexpr int kMaxUnison = 7;

    struct Patch
    {
        std::atomic<int>   unison        { 1 };       // 1..kMaxUnison
        std::atomic<float> stereoSpread  { 0.5f };    // 0..1 unison pan width
        std::atomic<float> subLevel      { 0.0f };    // 0..1
        std::atomic<float> noiseLevel    { 0.0f };    // 0..1
        std::atomic<float> fmAmount      { 0.0f };    // 0..1 blend + index
        std::atomic<float> fmRatio       { 2.0f };    // modulator ratio
        std::atomic<float> vibRateHz     { 0.0f };
        std::atomic<float> vibDepthCents { 0.0f };
        std::atomic<float> filterCutoff  { 20000.0f };// per-voice LP base
        std::atomic<float> filterEnvOct  { 0.0f };    // env sweep, in octaves
        std::atomic<float> filterEnvMs   { 200.0f };  // env decay time

        void resetToInit()
        {
            unison = 1; stereoSpread = 0.5f; subLevel = 0.0f; noiseLevel = 0.0f;
            fmAmount = 0.0f; fmRatio = 2.0f; vibRateHz = 0.0f; vibDepthCents = 0.0f;
            filterCutoff = 20000.0f; filterEnvOct = 0.0f; filterEnvMs = 200.0f;
        }
    };

    void prepare (juce::dsp::ProcessSpec spec);
    void reset();

    // Per-block settings from APVTS (audio thread).
    void setEnvelope (float attackMs, float decayMs, float sustain0to1, float releaseMs);
    void setWave (int waveType)      { wave = juce::jlimit (0, 3, waveType); }
    void setDetuneCents (float c)    { detuneCents = juce::jlimit (0.0f, 50.0f, c); }
    void setOctave (int oct)         { octave = juce::jlimit (-2, 2, oct); }

    /** Extended per-instrument settings (message-thread writable). */
    Patch& patch() { return patchSettings; }

    void noteOn  (int midiNote, float velocity);
    void noteOff (int midiNote);
    void tapNote (int midiNote, float velocity);   // auto-releases (UI clicks)
    void releaseAll();

    void render (juce::AudioBuffer<float>& out, int numSamples);

private:
    struct Voice
    {
        enum class Stage { attack, decay, sustain, release, idle };

        int    note = -1;
        float  velocity = 0.0f;

        // Oscillator bank.
        double phases[kMaxUnison] {};
        double incs[kMaxUnison] {};
        float  panL[kMaxUnison] {}, panR[kMaxUnison] {};
        int    unison = 1;
        float  unisonNorm = 1.0f;

        // Sub / FM / vibrato.
        double subPhase = 0.0,  subInc = 0.0;
        double fmCarPhase = 0.0, fmCarInc = 0.0;
        double fmModPhase = 0.0, fmModInc = 0.0;
        double vibPhase = 0.0,  vibInc = 0.0;

        // Snapshot of patch values at note start (RT-stable per note).
        float subLevel = 0.0f, noiseLevel = 0.0f, fmAmount = 0.0f;
        float vibDepthCents = 0.0f;

        // Per-voice lowpass (two one-poles in series per channel).
        float fltBaseHz = 20000.0f, fltEnvOct = 0.0f;
        float fenv = 0.0f;            // filter env level 1 -> 0
        float fenvCoeff = 1.0f;       // per-sample decay multiplier
        float lp1L = 0, lp2L = 0, lp1R = 0, lp2R = 0;
        float fltG = 1.0f;            // current filter coefficient
        int   fltUpdateCounter = 0;

        // Amp ADSR.
        Stage  stage = Stage::idle;
        int    stagePos = 0;
        float  releaseFrom = 1.0f;
        int    attackSamples = 1, decaySamples = 1, releaseSamples = 1;
        float  sustainLevel = 1.0f;

        int    autoOffCounter = -1;

        bool  isActive() const { return stage != Stage::idle; }
        float envelope() const;
    };

    float  renderOsc (double phase, double inc) const;
    Voice* findFreeVoice();
    void   startVoice (Voice&, int midiNote, float velocity, int autoOffSamples);

    static constexpr int kMaxVoices = 16;
    std::array<Voice, kMaxVoices> voices;

    Patch  patchSettings;
    juce::Random noiseRng;   // audio thread only

    double sampleRate = 44100.0;
    int    wave = Saw;
    int    octave = 0;
    float  detuneCents = 7.0f;

    float attackMs = 5.0f, decayMs = 120.0f, sustainLvl = 0.75f, releaseMs = 60.0f;
};
