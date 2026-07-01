#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

/**
    Granular texture generator.

    Captures the incoming signal into a history ring buffer and sprays
    overlapping Hann-windowed grains read from recent history with per-grain
    pan and position jitter. Output is blended with the dry signal by an
    internal mix control; at mix == 0 the engine is a pure pass-through, so it
    is safe to leave in the chain when granular mode is off.
*/
class GranularEngine
{
public:
    void prepare (juce::dsp::ProcessSpec spec);
    void reset();

    void setGrainSize (float milliseconds);
    void setMix (float m)      { mix = juce::jlimit (0.0f, 1.0f, m); }
    void setDensity (float d)  { density = juce::jlimit (0.05f, 1.0f, d); }

    void process (juce::AudioBuffer<float>& buffer);

private:
    struct Grain
    {
        long long readPos = 0;   // absolute position in the history timeline
        int   length = 0;
        int   age    = 0;
        float panL   = 0.7071f;
        float panR   = 0.7071f;
        bool  active = false;
    };

    void spawnGrain();
    float historyRead (int channel, long long absolutePos) const;

    juce::dsp::ProcessSpec spec {};
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxGrains   = 64;

    std::vector<float> history[kMaxChannels];
    long long writeAbs = 0;
    int historyLen = 0;

    std::array<Grain, kMaxGrains> grains;
    int   grainLenSamples = 2048;
    float density = 0.5f;
    float mix     = 0.0f;
    int   hopCounter = 0;
    juce::Random rng;
};
