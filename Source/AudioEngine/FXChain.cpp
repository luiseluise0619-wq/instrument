#include "FXChain.h"
#include <algorithm>
#include <cmath>

//==============================================================================
void DistortionFX::process (juce::AudioBuffer<float>& buffer, float drive)
{
    if (drive <= 0.0f)
        return;

    const float preGain  = 1.0f + drive * 24.0f;   // up to ~+28 dB
    const float makeup   = 1.0f / std::sqrt (preGain);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            const float clean = d[n];
            const float dirty = std::tanh (clean * preGain) * makeup;
            d[n] = clean * (1.0f - drive) + dirty * drive;
        }
    }
}

//==============================================================================
void ReverbFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    reverb.reset();
    reverb.prepare (spec);

    juce::dsp::Reverb::Parameters p;
    p.roomSize = 0.6f;
    p.damping  = 0.4f;
    p.wetLevel = 0.0f;
    p.dryLevel = 1.0f;
    p.width    = 1.0f;
    reverb.setParameters (p);
}

void ReverbFX::process (juce::AudioBuffer<float>& buffer, float amount)
{
    if (amount <= 0.0f)
        return;

    juce::dsp::Reverb::Parameters p;
    p.roomSize = 0.6f;
    p.damping  = 0.4f;
    p.wetLevel = juce::jlimit (0.0f, 1.0f, amount);
    p.dryLevel = 1.0f - amount * 0.5f;
    p.width    = 1.0f;
    reverb.setParameters (p);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    reverb.process (ctx);
}

//==============================================================================
void DelayFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    maxSamples = (int) (2.0 * sampleRate) + 4;
    for (auto& l : line)
        l.assign ((size_t) maxSamples, 0.0f);
    updateDelay();
    reset();
}

void DelayFX::reset()
{
    for (auto& l : line)
        std::fill (l.begin(), l.end(), 0.0f);
    writePos = 0;
}

void DelayFX::updateDelay()
{
    delaySamples = juce::jlimit (1, maxSamples - 1, (int) (delaySeconds * sampleRate));
}

void DelayFX::process (juce::AudioBuffer<float>& buffer, float amount)
{
    if (amount <= 0.0f)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples  = buffer.getNumSamples();
    const float wet = juce::jlimit (0.0f, 1.0f, amount);

    for (int n = 0; n < numSamples; ++n)
    {
        const int readPos = (writePos - delaySamples + maxSamples) % maxSamples;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& l = line[(size_t) ch];
            const float in      = buffer.getSample (ch, n);
            const float delayed = l[(size_t) readPos];

            l[(size_t) writePos] = in + delayed * feedback;
            buffer.setSample (ch, n, in + delayed * wet);
        }

        writePos = (writePos + 1) % maxSamples;
    }
}

//==============================================================================
void FXChain::prepare (juce::dsp::ProcessSpec s)
{
    spec = s;
    distortion.prepare (spec);
    reverb.prepare (spec);
    delay.prepare (spec);
}
