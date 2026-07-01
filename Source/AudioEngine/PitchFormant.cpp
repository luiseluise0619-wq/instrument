#include "PitchFormant.h"
#include <algorithm>
#include <cmath>

void PitchFormant::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    grainSamples = juce::jmax (256, (int) (0.050 * sr));
    bufferLength = grainSamples * 2 + 8;

    for (int ch = 0; ch < kMaxChannels; ++ch)
        delayBuffer[ch].assign ((size_t) bufferLength, 0.0f);

    dryBuffer.setSize (kMaxChannels, 512, false, false, true);
    reset();
}

void PitchFormant::reset()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        std::fill (delayBuffer[ch].begin(), delayBuffer[ch].end(), 0.0f);
        writeIdx[ch]  = 0;
        phase[ch]     = 0.0f;
        tiltState[ch] = 0.0f;
    }
}

void PitchFormant::setPitch (float semitones)
{
    pitchRatio = std::pow (2.0f, juce::jlimit (-24.0f, 24.0f, semitones) / 12.0f);
}

void PitchFormant::setFormant (float semitones)
{
    formantSemi = juce::jlimit (-12.0f, 12.0f, semitones);
}

float PitchFormant::readInterpolated (int channel, float delaySamples) const
{
    const auto& buf = delayBuffer[channel];
    float readPos = (float) writeIdx[channel] - 1.0f - delaySamples;
    while (readPos < 0.0f)                 readPos += (float) bufferLength;
    while (readPos >= (float) bufferLength) readPos -= (float) bufferLength;

    const int   i0 = (int) readPos;
    const int   i1 = (i0 + 1) % bufferLength;
    const float frac = readPos - (float) i0;
    return buf[(size_t) i0] + frac * (buf[(size_t) i1] - buf[(size_t) i0]);
}

void PitchFormant::processChannel (int channel, float* data, int numSamples)
{
    auto& buf   = delayBuffer[channel];
    auto& wIdx  = writeIdx[channel];
    auto& ph    = phase[channel];
    auto& tilt  = tiltState[channel];

    const float grain    = (float) grainSamples;
    const float deltaPh  = 1.0f - pitchRatio;   // change in delay per sample
    const float halfGrain = grain * 0.5f;

    // Formant tilt: one-pole split into low/high bands, then rebalance.
    const float tiltCoeff = 0.05f;              // ~cutoff for the tilt split
    const float bright     = std::pow (2.0f, formantSemi / 12.0f);
    const float lowGain    = 1.0f / bright;
    const float highGain   = bright;

    for (int n = 0; n < numSamples; ++n)
    {
        const float in = data[n];

        // Write current input into the delay line.
        buf[(size_t) wIdx] = in;

        // Advance the crossfade phase and keep it in [0, grain).
        ph += deltaPh;
        while (ph >= grain) ph -= grain;
        while (ph < 0.0f)   ph += grain;

        const float d1 = ph;
        float d2 = ph + halfGrain;
        if (d2 >= grain) d2 -= grain;

        // Complementary Hann windows (sum to unity).
        const float w1 = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * d1 / grain);
        const float w2 = 1.0f - w1;

        float wet = readInterpolated (channel, d1) * w1
                  + readInterpolated (channel, d2) * w2;

        // Spectral tilt (formant).
        tilt += tiltCoeff * (wet - tilt);
        const float high = wet - tilt;
        wet = tilt * lowGain + high * highGain;

        data[n] = wet;

        wIdx = (wIdx + 1) % bufferLength;
    }
}

void PitchFormant::process (juce::AudioBuffer<float>& buffer,
                            float pitchSemi, float formantSemi, float mix)
{
    setPitch (pitchSemi);
    setFormant (formantSemi);

    const int numChannels = juce::jmin (buffer.getNumChannels(), kMaxChannels);
    const int numSamples   = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0)
        return;

    // Bypass entirely at unity so we don't comb-filter the dry signal.
    const bool unity = std::abs (pitchSemi) < 0.01f && std::abs (formantSemi) < 0.01f;
    if (unity)
        return;

    mix = juce::jlimit (0.0f, 1.0f, mix);

    // Preserve dry for the wet/dry blend.
    dryBuffer.setSize (numChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
        processChannel (ch, buffer.getWritePointer (ch), numSamples);

    if (mix < 1.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* wet = buffer.getWritePointer (ch);
            const float* dry = dryBuffer.getReadPointer (ch);
            for (int n = 0; n < numSamples; ++n)
                wet[n] = dry[n] * (1.0f - mix) + wet[n] * mix;
        }
    }
}
