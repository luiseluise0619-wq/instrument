#include "GranularEngine.h"
#include <algorithm>
#include <cmath>

void GranularEngine::prepare (juce::dsp::ProcessSpec s)
{
    spec = s;
    historyLen = juce::jmax (1, (int) (2.0 * spec.sampleRate)); // 2 s of history
    for (int ch = 0; ch < kMaxChannels; ++ch)
        history[ch].assign ((size_t) historyLen, 0.0f);

    mixSmoothed.reset (spec.sampleRate, 0.025);
    setGrainSize (80.0f);
    reset();
}

void GranularEngine::reset()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
        std::fill (history[ch].begin(), history[ch].end(), 0.0f);
    mixSmoothed.setCurrentAndTargetValue (0.0f);
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
    if (absolutePos < 0 || absolutePos < writeAbs - historyLen || absolutePos > writeAbs)
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

    mixSmoothed.setTargetValue (mix);
    // Do not freeze live grains while bypassed and replay their old positions
    // when Texture is raised again. Keep only fresh input history.
    if (mix <= 0.0f && ! mixSmoothed.isSmoothing())
    {
        for (auto& grain : grains) grain.active = false;
        hopCounter = 0;
        // Still keep history current so enabling later has material to read.
        for (int n = 0; n < numSamples; ++n)
        {
            for (int ch = 0; ch < kMaxChannels; ++ch)
                history[ch][(size_t) (writeAbs % historyLen)] = buffer.getSample (juce::jmin (ch, numChannels - 1), n);
            ++writeAbs;
        }
        return;
    }

    const int hop = juce::jmax (1, (int) ((float) grainLenSamples * (1.0f - density * 0.75f)));

    float* outL = buffer.getWritePointer (0);
    float* outR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;


    for (int n = 0; n < numSamples; ++n)
    {
        const float wetMix = mixSmoothed.getNextValue();
        const float dry = 1.0f - wetMix;
        const float inL = outL[n];
        const float inR = outR != nullptr ? outR[n] : inL;

        // Write input into history.
        history[0][(size_t) (writeAbs % historyLen)] = inL;
        history[1][(size_t) (writeAbs % historyLen)] = inR;

        // Schedule grains.
        if (--hopCounter <= 0)
        {
            spawnGrain();
            hopCounter = hop;
        }

        // Render active grains.
        float wetL = 0.0f, wetR = 0.0f;
        float overlapL = 0.0f, overlapR = 0.0f;
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
            overlapL += win * g.panL;
            overlapR += win * g.panR;

            if (++g.age >= g.length)
                g.active = false;
        }

        // Follow the actual window overlap instead of assuming exactly two
        // grains. The floor preserves fades and never boosts a nearly silent
        // grain to full scale.
        wetL /= juce::jmax (1.0f, overlapL);
        wetR /= juce::jmax (1.0f, overlapR);

        outL[n] = inL * dry + wetL * wetMix;
        if (outR != nullptr)
            outR[n] = inR * dry + wetR * wetMix;

        ++writeAbs;
    }
}
