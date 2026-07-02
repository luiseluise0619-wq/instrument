#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

/**
    Polyphonic subtractive-style synth engine (the "Synth" engine mode, so the
    instrument makes sound with no sample loaded).

    - 2 oscillators per voice, the second detuned by a cents amount for width.
    - Waveforms: Saw / Square / Sine / Triangle. Saw and square use PolyBLEP
      so they don't alias.
    - Same ADSR feel as the sampler voices; noteOff starts the release.
    - tapNote() is for on-screen keyboard clicks: it auto-releases after a
      short hold so a click plays a musical note without needing a mouse-up.

    All setters are called from the audio thread once per block (like
    VoicePool); render() mixes additively into the output buffer. RT-safe:
    no allocation, locks or strings on the audio thread.
*/
class SynthEngine
{
public:
    enum Wave { Saw = 0, Square, Sine, Triangle };

    void prepare (juce::dsp::ProcessSpec spec);
    void reset();

    // Per-block settings adopted by new (and for wave/detune, running) voices.
    void setEnvelope (float attackMs, float decayMs, float sustain0to1, float releaseMs);
    void setWave (int waveType)      { wave = juce::jlimit (0, 3, waveType); }
    void setDetuneCents (float c)    { detuneCents = juce::jlimit (0.0f, 50.0f, c); }
    void setOctave (int oct)         { octave = juce::jlimit (-2, 2, oct); }

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
        double phase1 = 0.0, phase2 = 0.0;   // 0..1 oscillator phases
        double inc1 = 0.0, inc2 = 0.0;       // phase increment per sample

        Stage  stage = Stage::idle;
        int    stagePos = 0;
        float  releaseFrom = 1.0f;
        int    attackSamples = 1, decaySamples = 1, releaseSamples = 1;
        float  sustainLevel = 1.0f;

        int    autoOffCounter = -1;          // >=0: countdown to auto noteOff

        bool  isActive() const { return stage != Stage::idle; }
        float envelope() const;
    };

    float renderOsc (double phase, double inc) const;
    Voice* findFreeVoice();
    void   startVoice (Voice&, int midiNote, float velocity, int autoOffSamples);

    static constexpr int kMaxVoices = 16;
    std::array<Voice, kMaxVoices> voices;

    double sampleRate = 44100.0;
    int    wave = Saw;
    int    octave = 0;
    float  detuneCents = 7.0f;

    float attackMs = 5.0f, decayMs = 120.0f, sustainLvl = 0.75f, releaseMs = 60.0f;
};
