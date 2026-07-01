#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

/**
    Pitch and formant shaper.

    Pitch shifting uses a time-domain dual-tap variable-delay line with a
    half-window-offset Hann crossfade. The two taps' windows sum to unity, so
    there is no amplitude modulation and the delay-wrap discontinuity is masked.
    This is CPU-cheap and click-free — a pragmatic alternative to a full phase
    vocoder (for mastering-grade results, wire in SoundTouch / elastique here).

    Formant applies a first-order spectral tilt: positive values brighten
    (raise apparent formants), negative values darken.
*/
class PitchFormant
{
public:
    void prepare (double sampleRate);
    void reset();

    void setPitch (float semitones);
    void setFormant (float semitones);

    /** Processes in place, blending dry/wet by mix (0 = dry, 1 = wet). */
    void process (juce::AudioBuffer<float>& buffer,
                  float pitchSemi, float formantSemi, float mix);

private:
    void processChannel (int channel, float* data, int numSamples);
    float readInterpolated (int channel, float delaySamples) const;

    double sr = 44100.0;
    float  pitchRatio  = 1.0f;
    float  formantSemi = 0.0f;

    int    grainSamples = 2205;   // ~50 ms at 44.1 kHz
    int    bufferLength = 4410;

    static constexpr int kMaxChannels = 2;
    std::vector<float> delayBuffer[kMaxChannels];
    int   writeIdx[kMaxChannels] = { 0, 0 };
    float phase[kMaxChannels]    = { 0.0f, 0.0f };
    float tiltState[kMaxChannels] = { 0.0f, 0.0f };

    juce::AudioBuffer<float> dryBuffer;
};
