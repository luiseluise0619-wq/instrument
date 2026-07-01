#include "PitchFormant.h"
#include <algorithm>

void PitchFormant::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sr       = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = juce::jlimit (1, 2, numChannels);

    stretch.presetDefault (channels, (float) sr);
    stretch.reset();

    latency = stretch.inputLatency() + stretch.outputLatency();

    inputScratch.setSize (channels, juce::jmax (1, maxBlockSize), false, false, true);

    ringCap = juce::jmax (1, latency + juce::jmax (1, maxBlockSize) + 1);
    for (int ch = 0; ch < 2; ++ch)
        dryRing[(size_t) ch].assign ((size_t) ringCap, 0.0f);
    ringWrite = 0;
}

void PitchFormant::reset()
{
    stretch.reset();
    for (int ch = 0; ch < 2; ++ch)
        std::fill (dryRing[(size_t) ch].begin(), dryRing[(size_t) ch].end(), 0.0f);
    ringWrite = 0;
}

void PitchFormant::process (juce::AudioBuffer<float>& buffer,
                            float pitchSemitones, float formantSemitones, float mix)
{
    setPitch (pitchSemitones);
    setFormant (formantSemitones);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), channels);
    if (numSamples == 0 || numChannels == 0)
        return;

    mix = juce::jlimit (0.0f, 1.0f, mix);

    // Apply pitch/formant on the audio thread (cheap parameter setters).
    stretch.setTransposeSemitones (pitchSemi);
    stretch.setFormantSemitones (formantSemi, true); // compensate = preserve character

    // Keep an untouched dry copy, then stretch it into the output buffer.
    for (int ch = 0; ch < numChannels; ++ch)
        inputScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    float* inPtrs[2]  = { nullptr, nullptr };
    float* outPtrs[2] = { nullptr, nullptr };
    for (int ch = 0; ch < numChannels; ++ch)
    {
        inPtrs[ch]  = inputScratch.getWritePointer (ch);
        outPtrs[ch] = buffer.getWritePointer (ch);
    }

    stretch.process (inPtrs, numSamples, outPtrs, numSamples);

    // Latency-matched dry/wet blend (the ring always advances so switching the
    // mix at runtime stays coherent).
    for (int n = 0; n < numSamples; ++n)
    {
        int readIdx = ringWrite - latency;
        if (readIdx < 0)
            readIdx += ringCap;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            dryRing[(size_t) ch][(size_t) ringWrite] = inputScratch.getSample (ch, n);

            if (mix < 1.0f)
            {
                const float delayedDry = dryRing[(size_t) ch][(size_t) readIdx];
                const float wet = buffer.getSample (ch, n);
                buffer.setSample (ch, n, delayedDry * (1.0f - mix) + wet * mix);
            }
        }

        ringWrite = (ringWrite + 1) % ringCap;
    }
}
