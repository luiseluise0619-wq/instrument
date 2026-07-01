#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

/**
    A single polyphonic voice. Plays back a region [start, start+len) of a
    shared source buffer, resampled from the sample's native rate to the host
    rate (linear interpolation), with an attack ramp and a short release fade so
    retriggering never clicks.

    The voice holds a shared_ptr to the source buffer, so the buffer stays alive
    for the voice's whole lifetime even if a new sample is loaded mid-note.
*/
class Voice
{
public:
    void start (double hostSampleRate,
                std::shared_ptr<const juce::AudioBuffer<float>> src, double srcSampleRate,
                int startSample, int lengthSamples,
                float velocity, float attackMs);

    void render (juce::AudioBuffer<float>& out, int numSamples);
    void release();

    bool isActive() const { return active; }

private:
    float envelope() const;

    std::shared_ptr<const juce::AudioBuffer<float>> source;
    const float* srcL = nullptr;
    const float* srcR = nullptr;
    int   srcNumSamples = 0;

    double pos = 0.0;          // fractional read position (source samples)
    double start = 0.0;        // slice start (source samples)
    double length = 0.0;       // slice length (source samples)
    double ratio = 1.0;        // srcSampleRate / hostSampleRate

    int   outPos = 0;          // output samples elapsed (for the attack ramp)
    int   attackSamples = 1;   // in output samples
    int   releaseSamples = 1;  // in output samples
    int   releasePos = -1;     // >= 0 once releasing (note-off)

    float velocity = 1.0f;
    bool  active = false;
};

/**
    Fixed-size pool of voices sharing one source buffer.

    triggerVoice() runs on the audio thread only (MIDI and the processor's
    lock-free pad queue both feed it there). setSource() runs on the message
    thread and is guarded by a spin lock; triggerVoice() takes the lock with a
    try-lock and simply drops the trigger on the rare contended block.
*/
class VoicePool
{
public:
    void prepare (juce::dsp::ProcessSpec spec);
    void setSource (std::shared_ptr<const juce::AudioBuffer<float>> src, double sampleRate);

    void triggerVoice (int startSample, int lengthSamples, float velocity, float attackMs);
    void releaseAll();
    void renderNextBlock (juce::AudioBuffer<float>& out, int numSamples);

private:
    static constexpr int kMaxVoices = 16;
    std::array<Voice, kMaxVoices> voices;

    std::shared_ptr<const juce::AudioBuffer<float>> source;
    double sourceSampleRate = 44100.0;
    double hostSampleRate   = 44100.0;

    juce::SpinLock sourceLock;
};
