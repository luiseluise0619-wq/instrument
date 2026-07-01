#include "FXChain.h"
#include <algorithm>
#include <cmath>

//==============================================================================
void DistortionFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    smoothedDrive.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedDrive.setCurrentAndTargetValue (0.0f);
}

void DistortionFX::process (juce::AudioBuffer<float>& buffer, float drive)
{
    const float target = juce::jlimit (0.0f, 1.0f, drive);
    smoothedDrive.setTargetValue (target);

    // Safe idle skip: only bail if the effect is fully off AND not mid-ramp,
    // otherwise we'd chop the tail of a fade-out.
    if (target <= 0.0f && ! smoothedDrive.isSmoothing())
    {
        smoothedDrive.setCurrentAndTargetValue (0.0f);
        return;
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Snapshot the smoother start state so every channel gets the same ramp.
    const auto startDrive = smoothedDrive;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto smoother = startDrive; // per-channel copy of the ramp
        float* d = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            const float dNow    = smoother.getNextValue();
            const float preGain = 1.0f + dNow * 24.0f;   // up to ~+28 dB
            const float makeup  = 1.0f / std::sqrt (preGain);

            const float clean = d[n];
            const float dirty = std::tanh (clean * preGain) * makeup;
            d[n] = clean * (1.0f - dNow) + dirty * dNow;
        }

        // Keep the shared smoother advanced to the block-end state after the
        // last channel so its position stays consistent across blocks.
        if (ch == numChannels - 1)
            smoothedDrive = smoother;
    }

    // If the buffer had no channels, still advance the smoother by the block.
    if (numChannels == 0)
        smoothedDrive.skip (numSamples);
}

//==============================================================================
void ReverbFX::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    reverb.reset();
    reverb.prepare (spec);

    smoothedAmount.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedAmount.setCurrentAndTargetValue (0.0f);

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
    const float target = juce::jlimit (0.0f, 1.0f, amount);
    smoothedAmount.setTargetValue (target);

    // Safe idle skip: only bail if fully off and not still ramping down.
    if (target <= 0.0f && ! smoothedAmount.isSmoothing())
    {
        smoothedAmount.setCurrentAndTargetValue (0.0f);
        return;
    }

    const int numSamples = buffer.getNumSamples();

    // juce::dsp::Reverb updates smoothly enough per block; advance the smoother
    // across the block (never hard-jump) and use the block-end value.
    smoothedAmount.skip (numSamples);
    const float wet = smoothedAmount.getCurrentValue();

    juce::dsp::Reverb::Parameters p;
    p.roomSize = 0.6f;
    p.damping  = 0.4f;
    p.wetLevel = wet;
    p.dryLevel = 1.0f - wet * 0.5f;
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

    smoothedWet.reset (sampleRate, 0.02); // ~20 ms ramp
    smoothedWet.setCurrentAndTargetValue (0.0f);
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
    const float target = juce::jlimit (0.0f, 1.0f, amount);
    smoothedWet.setTargetValue (target);

    // Safe idle skip: only bail if fully off and not still ramping down; a
    // fade-out still needs to feed the delay line and mix its tail.
    if (target <= 0.0f && ! smoothedWet.isSmoothing())
    {
        smoothedWet.setCurrentAndTargetValue (0.0f);
        return;
    }

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples  = buffer.getNumSamples();

    for (int n = 0; n < numSamples; ++n)
    {
        const float wet     = smoothedWet.getNextValue(); // per-sample wet mix
        const int   readPos = (writePos - delaySamples + maxSamples) % maxSamples;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& l = line[(size_t) ch];
            const float in      = buffer.getSample (ch, n);
            const float delayed = l[(size_t) readPos];

            l[(size_t) writePos] = in + delayed * feedback; // feedback per-block scalar
            buffer.setSample (ch, n, in + delayed * wet);
        }

        writePos = (writePos + 1) % maxSamples;
    }

    // If fewer than expected channels were present, keep the smoother advanced.
    if (numChannels == 0)
        smoothedWet.skip (numSamples);
}

//==============================================================================
void FXChain::prepare (juce::dsp::ProcessSpec s)
{
    spec = s;
    distortion.prepare (spec);
    reverb.prepare (spec);
    delay.prepare (spec);
}
