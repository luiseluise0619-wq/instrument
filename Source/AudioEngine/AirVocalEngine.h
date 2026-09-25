#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <limits>
#include <vector>

/**
    Chromatic, polyphonic CORE + AIR vocal instrument with one shared HALO bus.

    Source decoding and bank publication happen off the audio callback. The
    callback reads an immutable shared bank, fixed voices and delay lines that
    were allocated in prepare(). There is deliberately no granular or formant
    claim here: VOWEL is a real crossfade between three distinct source files.
*/
class AirVocalEngine
{
public:
    enum Macro { Air = 0, Body, Vowel, Bloom, Motion, Space, NumMacros };

    struct Source
    {
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        double sampleRate = 44100.0;
        int rootNote = 60;
        float loopStart = 0.16f;
        float loopEnd = 0.82f;
        float crossfadeMs = 70.0f;
        juce::String name;
    };

    struct Bank
    {
        std::array<Source, 3> core; // actual Ah/Oo/Mm-style sources
        Source air;                 // separate breath/texture source
        juce::String presetName;
    };

    struct PresetShape
    {
        float attackMs = 90.0f;
        float releaseMs = 850.0f;
        float coreGain = 0.72f;
        float airGain = 0.16f;
        float haloSend = 0.28f;
        float haloDecay = 0.76f;
        float haloPredelayMs = 28.0f;
        float brightnessHz = 9500.0f;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void setBank (std::shared_ptr<const Bank>);
    void setPresetShape (const PresetShape&) noexcept;
    void setMacros (float air, float body, float vowel, float bloom,
                    float motion, float space) noexcept;

    void noteOn (int midiNote, float velocity, bool selfReleasing = false) noexcept;
    void noteOff (int midiNote) noexcept;
    void releaseAll() noexcept;
    void stopAll() noexcept;
    void render (juce::AudioBuffer<float>&, int numSamples) noexcept;

    int getActiveVoiceCount() const noexcept;
    juce::String getPresetName() const;

private:
    struct Voice
    {
        std::shared_ptr<const Bank> bank;
        std::array<double, 3> corePos {};
        double airPos = 0.0;
        std::array<float, 3> lpL {}, lpR {};
        float airLpL = 0.0f, airLpR = 0.0f;
        float env = 0.0f, releaseCoeff = 0.999f, velocity = 0.8f;
        float panL = 0.707f, panR = 0.707f;
        float motionPhase = 0.0f, motionRate = 0.2f;
        uint64_t age = 0;
        int note = -1;
        bool releasing = false, selfReleasing = false;
        bool active() const noexcept { return bank != nullptr; }
        void stop() noexcept { *this = {}; }
    };

    static constexpr int kMaxVoices = 24;
    std::array<Voice, kMaxVoices> voices {};
    std::shared_ptr<const Bank> bank { std::make_shared<Bank>() };
    uint64_t voiceCounter = 0;
    double sr = 44100.0;

    PresetShape shape;
    std::array<float, NumMacros> macros { 0.25f, 0.55f, 0.35f, 0.35f, 0.18f, 0.40f };

    std::vector<float> preL, preR, haloL, haloR;
    int preWrite = 0, haloWrite = 0;
    float haloLowL = 0.0f, haloLowR = 0.0f;
    float haloDcL = 0.0f, haloDcR = 0.0f;
    float duckEnv = 0.0f;

    Voice* acquireVoice() noexcept;
    static float sampleLinear (const Source&, int channel, double position) noexcept;
    static float sampleLooped (const Source&, int channel, double position) noexcept;
    static double wrapPosition (const Source&, double position) noexcept;
    void renderHalo (juce::AudioBuffer<float>&, int sampleIndex,
                     float sendL, float sendR, float dryLevel) noexcept;
};
