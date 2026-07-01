#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

/**
    A single polyphonic voice. Plays back a region [start, start+len) of a
    shared source buffer with an attack ramp and a short release fade so
    retriggering never clicks.
*/
class Voice
{
public:
    void start (double sr,
                const juce::AudioBuffer<float>* src,
                int startSample, int lengthSamples,
                float velocity, float attackMs);

    void render (juce::AudioBuffer<float>& out, int numSamples);
    void release();

    bool isActive() const { return active; }
    int  getStartSample() const { return start; }

private:
    const juce::AudioBuffer<float>* source = nullptr;
    int   start = 0, length = 0, pos = 0;
    int   attackSamples = 0;
    int   releaseSamples = 0;
    int   releasePos = -1;         // >= 0 once releasing
    float velocity = 1.0f;
    double sampleRate = 44100.0;
    bool  active = false;

    float envelopeAt (int position) const;
};

/**
    Fixed-size pool of voices sharing one source buffer. Steals the oldest
    voice when all are busy.
*/
class VoicePool
{
public:
    void prepare (juce::dsp::ProcessSpec spec);
    void setSource (std::shared_ptr<juce::AudioBuffer<float>> src);

    void triggerVoice (int startSample, int lengthSamples,
                       float velocity, float attackMs, double sr);
    void releaseAll();
    void renderNextBlock (juce::AudioBuffer<float>& out, int numSamples);

private:
    static constexpr int kMaxVoices = 16;
    std::array<Voice, kMaxVoices> voices;
    std::shared_ptr<juce::AudioBuffer<float>> source;   // keeps the buffer alive
    double sampleRate = 44100.0;
};
