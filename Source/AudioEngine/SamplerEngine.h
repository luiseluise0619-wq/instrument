#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/**
    Multisample player (SFZ subset) — the "Sampled" engine mode.

    This is how Keyscape-class realism actually happens: real recordings,
    one per key range and velocity layer, not synthesis. Point it at a .sfz
    file (Salamander Grand Piano, free guitars...) and every region's WAV /
    FLAC / OGG is decoded into RAM up front; playback is a lightweight
    linear-interpolating resampler per voice.

    Supported opcodes (covers Salamander + most free instrument banks):
      sample, default_path, lokey, hikey, key, pitch_keycenter,
      lovel, hivel, loop_mode (loop_continuous), loop_start, loop_end,
      ampeg_release, tune, volume

    Thread model: loadSfz() builds a complete Bank on the message thread and
    swaps it in with an atomic shared_ptr; voices hold a shared_ptr to the
    bank they started on, so a re-load never frees buffers under a playing
    voice.
*/
class SamplerEngine
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& v : voices)
            v = {};
    }

    /** Message thread. Returns false and fills `error` on failure. */
    bool loadSfz (const juce::File& sfzFile, juce::String& error);

    /** Message thread. Builds a one-region bank from an in-memory sample so
        a loaded chop sample can be played chromatically — this is the whole
        "Melody" engine mode. rootNote = the MIDI key that plays the sample
        at its original pitch. */
    void loadFromBuffer (const juce::AudioBuffer<float>& src, double srcRate,
                         const juce::String& name, int rootNote = 60);

    bool hasBank() const { return std::atomic_load (&bank) != nullptr; }
    juce::String getBankName() const
    {
        if (auto b = std::atomic_load (&bank)) return b->name;
        return {};
    }

    // Audio thread ----------------------------------------------------------
    void noteOn (int midiNote, float velocity, int autoOffSamples = -1);
    /** One-shot tap (chord bar / pad taps): holds ~1.2 s then releases —
        without this a tapped piano note rings its FULL 30 s sample. */
    void tapNote (int midiNote, float velocity)
    {
        noteOn (midiNote, velocity, (int) (1.2 * sr));
    }
    void noteOff (int midiNote);
    void releaseAll();
    void render (juce::AudioBuffer<float>& out, int numSamples);

private:
    struct Region
    {
        juce::AudioBuffer<float> data;
        double srcRate  = 44100.0;
        int    lokey = 0, hikey = 127, root = 60;
        int    lovel = 0, hivel = 127;
        float  tuneCents = 0.0f;
        float  volumeDb  = 0.0f;
        bool   loop = false;
        int    loopStart = 0, loopEnd = 0;
        int    offset = 0;              // sample start position
        int    seqLen = 1, seqPos = 1;  // round-robin slot
        bool   onAttack = true;         // false: trigger=release/legato region
        float  releaseSeconds = 0.35f;
    };

    struct Bank
    {
        juce::String name;
        std::vector<Region> regions;
    };

    struct Voice
    {
        std::shared_ptr<const Bank> hold;   // keeps the bank alive
        const Region* region = nullptr;
        double pos = 0.0, ratio = 1.0;
        float  gain = 0.0f;
        int    note = -1;
        bool   releasing = false;
        float  env = 1.0f, relCoeff = 0.0f;
        int    fadeIn = 0;
        int    autoOff = -1;   // samples until self-release (-1 = held note)

        bool active() const { return region != nullptr; }
    };

    static constexpr int kMaxVoices = 24;

    const Region* findRegion (const Bank&, int note, int vel127);
    Voice* findFreeVoice();
    int rrCounter = 0;   // round-robin step (audio thread only)

    std::shared_ptr<const Bank> bank;   // swapped atomically
    std::array<Voice, kMaxVoices> voices;
    double sr = 44100.0;
};
