#include "GranularEngine.h"
#include <algorithm>
#include <cmath>

void GranularEngine::prepare (juce::dsp::ProcessSpec s)
{
    spec = s;
    historyLen = juce::jmax (1, (int) (2.0 * spec.sampleRate)); // 2 s of history
    for (int ch = 0; ch < kMaxChannels; ++ch)
        history[ch].assign ((size_t) historyLen, 0.0f);

    setGrainSize (80.0f);
    reset();
}

void GranularEngine::reset()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
        std::fill (history[ch].begin(), history[ch].end(), 0.0f);
    writeAbs = 0;
    hopCounter = 0;
    for (auto& g : grains)
        g.active = false;
}

void GranularEngine::setGrainSize (float ms)
{
    grainLenSamples = (int) (ms * 0.001f * (float) spec.sampleRate);
    grainLenSamples = juce::jlimit (64, 8192, grainLenSamples);
}

float GranularEngine::historyRead (int channel, long long absolutePos) const
{
    if (absolutePos < 0)
        return 0.0f;
    const int idx = (int) (absolutePos % historyLen);
    return history[channel][(size_t) idx];
}

void GranularEngine::spawnGrain()
{
    for (auto& g : grains)
    {
        if (! g.active)
        {
            const int jitter = (int) (rng.nextFloat() * (float) grainLenSamples);
            g.length = grainLenSamples;
            g.age    = 0;
            g.readPos = writeAbs - (long long) grainLenSamples - jitter;
            g.active = g.readPos >= 0;

            const float pan = rng.nextFloat();               // 0..1
            g.panL = std::cos (pan * juce::MathConstants<float>::halfPi);
            g.panR = std::sin (pan * juce::MathConstants<float>::halfPi);
            return;
        }
    }
}

void GranularEngine::process (juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin (buffer.getNumChannels(), kMaxChannels);
    const int numSamples  = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0 || historyLen == 0)
        return;

    // Pure pass-through when disabled.
    if (mix <= 0.0f)
    {
        // Still keep history current so enabling later has material to read.
        for (int n = 0; n < numSamples; ++n)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                history[ch][(size_t) (writeAbs % historyLen)] = buffer.getSample (ch, n);
            ++writeAbs;
        }
        return;
    }

    const int hop = juce::jmax (1, (int) ((float) grainLenSamples * (1.0f - density * 0.75f)));

    float* outL = buffer.getWritePointer (0);
    float* outR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    const float dry = 1.0f - mix;

    for (int n = 0; n < numSamples; ++n)
    {
        const float inL = outL[n];
        const float inR = outR != nullptr ? outR[n] : inL;

        // Write input into history.
        history[0][(size_t) (writeAbs % historyLen)] = inL;
        if (numChannels > 1)
            history[1][(size_t) (writeAbs % historyLen)] = inR;

        // Schedule grains.
        if (--hopCounter <= 0)
        {
            spawnGrain();
            hopCounter = hop;
        }

        // Render active grains.
        float wetL = 0.0f, wetR = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active)
                continue;

            const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                      * (float) g.age / (float) g.length);
            const long long pos = g.readPos + g.age;
            const float sL = historyRead (0, pos);
            const float sR = numChannels > 1 ? historyRead (1, pos) : sL;

            wetL += sL * win * g.panL;
            wetR += sR * win * g.panR;

            if (++g.age >= g.length)
                g.active = false;
        }

        // Compensate for grain overlap (roughly 2 grains overlapping).
        wetL *= 0.5f;
        wetR *= 0.5f;

        outL[n] = inL * dry + wetL * mix;
        if (outR != nullptr)
            outR[n] = inR * dry + wetR * mix;

        ++writeAbs;
    }
}
